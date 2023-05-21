/** fmedia: HTTP server
2023, Simon Zolin */

#include <fmedia.h>
#include <alphahttpd.h>
#include <util/ipaddr.h>
#include <http/client.h>
#include <FFOS/thread.h>
#include <FFOS/ffos-extern.h>

static const fmed_core *core;
#define HTTPCTL_PORT_DEFAULT  7314

struct htsv {
	alphahttpd *ah;
	struct alphahttpd_conf conf;
	struct alphahttpd_address laddr[2];
	const fmed_track *track;
	const fmed_queue *queue;
	char *www_path;
	fftask t;
	ffthread thr;
	uint cmd;
};
struct htsv *g;

enum CMD {
	CMD_PAUSE,
	CMD_UNPAUSE,
	CMD_NEXT,
};

/**
Supports only 1 command at a time. */
static void task_core(struct htsv *h)
{
	switch (h->cmd) {
	case CMD_PAUSE:
		h->track->cmd((void*)-1, FMED_TRACK_PAUSE);
		break;

	case CMD_UNPAUSE:
		h->track->cmd((void*)-1, FMED_TRACK_UNPAUSE);
		break;

	case CMD_NEXT:
		h->queue->cmdv(FMED_QUE_NEXT2, NULL);
		break;
	}
}

static void api_pause(alphahttpd_client *c)
{
	struct htsv *h = c->conf->opaque;
	h->cmd = CMD_PAUSE;
	core->task(&h->t, FMED_TASK_POST);
}

static void api_unpause(alphahttpd_client *c)
{
	struct htsv *h = c->conf->opaque;
	h->cmd = CMD_UNPAUSE;
	core->task(&h->t, FMED_TASK_POST);
}

static void api_next(alphahttpd_client *c)
{
	struct htsv *h = c->conf->opaque;
	h->cmd = CMD_NEXT;
	core->task(&h->t, FMED_TASK_POST);
}

static const struct alphahttpd_virtdoc routes[] = {
	{ "/api/pause", "POST", api_pause },
	{ "/api/unpause", "POST", api_unpause },
	{ "/api/next", "POST", api_next },
	{}
};

extern const struct alphahttpd_filter* ah_filters[];

static uint log_level_fmed(uint level)
{
	static const uint fmed_levels[] = {
		/*ALPHAHTTPD_LOG_SYSFATAL*/	FMED_LOG_ERR | FMED_LOG_SYS,
		/*ALPHAHTTPD_LOG_SYSERR*/	FMED_LOG_ERR | FMED_LOG_SYS,
		/*ALPHAHTTPD_LOG_ERR*/	FMED_LOG_ERR,
		/*ALPHAHTTPD_LOG_SYSWARN*/	FMED_LOG_WARN | FMED_LOG_SYS,
		/*ALPHAHTTPD_LOG_WARN*/	FMED_LOG_WARN,
		/*ALPHAHTTPD_LOG_INFO*/	FMED_LOG_INFO,
		/*ALPHAHTTPD_LOG_VERBOSE*/	FMED_LOG_INFO,
		/*ALPHAHTTPD_LOG_DEBUG*/	FMED_LOG_DEBUG,
	};
	return fmed_levels[level];
}

static uint log_level_ah(uint level)
{
	static const uint ah_levels[] = {
		/*FMED_LOG_ERR*/	ALPHAHTTPD_LOG_ERR,
		/*FMED_LOG_WARN*/	ALPHAHTTPD_LOG_WARN,
		/*FMED_LOG_USER*/	ALPHAHTTPD_LOG_VERBOSE,
		/*FMED_LOG_INFO*/	ALPHAHTTPD_LOG_VERBOSE,
		/*FMED_LOG_DEBUG*/	ALPHAHTTPD_LOG_DEBUG,
	};
	return ah_levels[level - FMED_LOG_ERR];
}

static void ah_logv(void *opaque, uint level, const char *id, const char *format, va_list va)
{
	core->logv(log_level_fmed(level), NULL, "http-ctl", format, va);
}

static void ah_log(void *opaque, uint level, const char *id, const char *format, ...)
{
	va_list va;
	va_start(va, format);
	core->logv(log_level_fmed(level), NULL, "http-ctl", format, va);
	va_end(va);
}

static int htsv_worker(void *param)
{
	struct htsv *h = param;
	if (0 != alphahttpd_run(h->ah))
		return -1;
	return 0;
}

static int htsv_start(struct htsv *h)
{
	h->queue = core->getmod("#queue.queue");
	h->track = core->getmod("#core.track");

	h->t.handler = (fftask_handler)task_core;
	h->t.param = h;

	struct alphahttpd_conf *ac = &h->conf;
	alphahttpd_conf(NULL, ac);
	ac->opaque = h;
	ac->log_level = log_level_ah(core->loglev);
	ac->log = ah_log;
	ac->logv = ah_logv;

	h->www_path = core->getpath("www", 3);
	ffstr_setz(&ac->fs.www, h->www_path);

	ac->server.listen_addresses = h->laddr;

	alphahttpd_filter_virtspace_init(ac, routes);

	ffvec content_types = {};
	ffvec_addsz(&content_types, "text/html	html\r\n");
	alphahttpd_filter_file_init(ac, *(ffstr*)&content_types);

	ac->filters = ah_filters;

	h->ah = alphahttpd_new();
	if (0 != alphahttpd_conf(h->ah, ac))
		goto end;

	if (FFTHREAD_NULL == (h->thr = ffthread_create(htsv_worker, h, 0))) {
		fmed_syserrlog(core, NULL, "http-ctl", "thread create");
		goto end;
	}
	return 0;

end:
	return -1;
}

static int htsv_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		if (0 != ffsock_init(FFSKT_SIGPIPE | FFSKT_WSA | FFSKT_WSAFUNCS))
			return -1;
		g = ffmem_new(struct htsv);
		g->thr = FFTHREAD_NULL;
		g->laddr[0].port = HTTPCTL_PORT_DEFAULT;
		break;

	case FMED_OPEN:
		return htsv_start(g);
	}
	return 0;
}

static void htsv_destroy(void)
{
	if (g == NULL) return;

	struct htsv *h = g;
	if (h->ah != NULL)
		alphahttpd_stop(h->ah);
	if (h->thr != FFTHREAD_NULL
		&& 0 != ffthread_join(h->thr, -1, NULL))
		fmed_syswarnlog(core, NULL, "http-ctl", "thread join");
	alphahttpd_free(h->ah);
	alphahttpd_filter_virtspace_uninit(&h->conf);
	ffmem_free(h->www_path);
	ffmem_free(g);
}

/** Not a canonical way to configure module, but works for now */
static int htsv_conf(const char *options, fmed_conf_ctx *ctx)
{
	ffstr o = FFSTR_INITZ(options), opt;
	while (o.len) {
		ffstr_splitby(&o, ';', &opt, &o);
		ffstr_trimwhite(&opt);
		if (!opt.len)
			continue;
		if (1) {
			if (ffip_port_split(opt, g->laddr[0].ip, &g->laddr[0].port) < 0) {
				fmed_errlog(core, NULL, "http-ctl", "Please specify correct IP:port");
				return -1;
			}
		}
	}
	return 0;
}

static const fmed_mod fmed_htsv = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.sig = htsv_sig,
	.destroy = htsv_destroy,
	.conf = htsv_conf,
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_htsv;
}
