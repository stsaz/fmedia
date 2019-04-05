/** HTTP, ICY input;  ICY stream-copy filter;  HTTP client interface.
Copyright (c) 2016 Simon Zolin */

#include <net/net.h>
#include <FF/net/http-client.h>
#include <FF/audio/icy.h>
#include <FF/net/url.h>
#include <FF/net/http.h>
#include <FF/data/utf8.h>
#include <FF/list.h>
#include <FF/path.h>
#include <FFOS/asyncio.h>
#include <FFOS/socket.h>
#include <FFOS/error.h>

netmod *net;
const fmed_core *core;

typedef struct icy icy;

typedef struct netin {
	uint state;
	void *trk;
	ffarr d[2];
	uint idx;
	uint fin :1;
	uint fn_dyn :1;
	icy *c;
} netin;

struct icy {
	fmed_filt *d;
	fficy icy;
	ffstr data;
	ffstr next_filt_ext;

	netin *netin;
	ffstr artist;
	ffstr title;

	uint out_copy :1;
	uint save_oncmd :1;
};

//FMEDIA MODULE
static const void* net_iface(const char *name);
static int net_mod_conf(const char *name, ffpars_ctx *ctx);
static int net_sig(uint signo);
static void net_destroy(void);
static const fmed_mod fmed_net_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&net_iface, &net_sig, &net_destroy, &net_mod_conf
};

//HTTP
static int http_config(ffpars_ctx *ctx);
static void* httpcli_open(fmed_filt *d);
static int httpcli_process(void *ctx, fmed_filt *d);
static void httpcli_close(void *ctx);
static const fmed_filter fmed_http = {
	&httpcli_open, &httpcli_process, &httpcli_close
};

static int http_conf_proxy(ffparser_schem *p, void *obj, ffstr *val);
static int http_conf_done(ffparser_schem *p, void *obj);

// HTTP IFACE
static void* http_if_request(const char *method, const char *url, uint flags);
static void http_if_close(void *con);
static void http_if_sethandler(void *con, ffhttpcl_handler func, void *udata);
static void http_if_send(void *con, const ffstr *data);
static int http_if_recv(void *con, ffhttp_response **resp, ffstr *data);
static void http_if_header(void *con, const ffstr *name, const ffstr *val, uint flags);
static void http_if_conf(void *con, struct ffhttpcl_conf *conf, uint flags);
const fmed_net_http http_iface = {
	&http_if_request, &http_if_close, &http_if_sethandler, &http_if_send, &http_if_recv, &http_if_header,
	&http_if_conf,
};

//ICY
static void* icy_open(fmed_filt *d);
static int icy_process(void *ctx, fmed_filt *d);
static void icy_close(void *ctx);
static int icy_config(ffpars_ctx *ctx);
static const fmed_filter fmed_icy = {
	&icy_open, &icy_process, &icy_close
};

static int icy_reset(icy *c, fmed_filt *d);
static int icy_setmeta(icy *c, const ffstr *_data);

//PASS-THROUGH
static void* netin_open(fmed_filt *d);
static int netin_process(void *ctx, fmed_filt *d);
static void netin_close(void *ctx);
static const fmed_filter fmed_netin = {
	&netin_open, &netin_process, &netin_close
};

static void* netin_create(icy *c);
static void netin_write(netin *n, const ffstr *data);


enum {
	UA_OFF,
	UA_NAME,
	UA_NAMEVER,
};

const char *const http_ua[] = {
	"fmedia", "fmedia/" FMED_VER
};

static const char *const ua_enumstr[] = {
	"off", "name_only", "full",
};
static const ffpars_enumlist ua_enum = { ua_enumstr, FFCNT(ua_enumstr), FFPARS_DSTOFF(net_conf, user_agent) };

static const ffpars_arg net_conf_args[] = {
	{ "bufsize",	FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(net_conf, bufsize) },
	{ "buffers",	FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(net_conf, nbufs) },
	{ "buffer_lowat",	FFPARS_TSIZE,  FFPARS_DSTOFF(net_conf, buf_lowat) },
	{ "connect_timeout",	FFPARS_TINT,  FFPARS_DSTOFF(net_conf, conn_tmout) },
	{ "timeout",	FFPARS_TINT,  FFPARS_DSTOFF(net_conf, tmout) },
	{ "user_agent",	FFPARS_TENUM | FFPARS_F8BIT,  FFPARS_DST(&ua_enum) },
	{ "max_redirect",	FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(net_conf, max_redirect) },
	{ "max_reconnect",	FFPARS_TINT8,  FFPARS_DSTOFF(net_conf, max_reconnect) },
	{ "proxy",	FFPARS_TSTR,  FFPARS_DST(&http_conf_proxy) },
	{ NULL,	FFPARS_TCLOSE,	FFPARS_DST(&http_conf_done) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_net_mod;
}


static const void* net_iface(const char *name)
{
	if (!ffsz_cmp(name, "icy"))
		return &fmed_icy;
	else if (!ffsz_cmp(name, "in"))
		return &fmed_netin;
	else if (!ffsz_cmp(name, "http"))
		return &fmed_http;
	else if (!ffsz_cmp(name, "httpif"))
		return &http_iface;
	else if (!ffsz_cmp(name, "hls"))
		return &nethls;
	return NULL;
}

static int net_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "icy"))
		return icy_config(ctx);
	else if (!ffsz_cmp(name, "http"))
		return http_config(ctx);
	return -1;
}

static int net_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		if (0 != ffskt_init(FFSKT_SIGPIPE | FFSKT_WSA | FFSKT_WSAFUNCS))
			return -1;
		if (NULL == (net = ffmem_tcalloc1(netmod)))
			return -1;
		ffhttp_initheaders();
		return 0;

	case FMED_OPEN:
		if (NULL == (net->qu = core->getmod("#queue.queue")))
			return 1;
		net->track = core->getmod("#core.track");
		break;
	}
	return 0;
}

static void net_destroy(void)
{
	if (net == NULL)
		return;
	ffhttpcl_deinit();
	ffmem_free(net->conf.proxy.host);
	ffmem_free0(net);
	ffhttp_freeheaders();
}


static int http_config(ffpars_ctx *ctx)
{
	net->conf.bufsize = 16 * 1024;
	net->conf.nbufs = 2;
	net->conf.buf_lowat = 8 * 1024;
	net->conf.conn_tmout = 1500;
	net->conf.tmout = 5000;
	net->conf.user_agent = UA_OFF;
	net->conf.max_redirect = 10;
	net->conf.max_reconnect = 3;
	ffpars_setargs(ctx, &net->conf, net_conf_args, FFCNT(net_conf_args));
	return 0;
}

static int http_conf_proxy(ffparser_schem *p, void *obj, ffstr *val)
{
	ffurl u;
	ffurl_init(&u);
	int r = ffurl_parse(&u, val->ptr, val->len);
	if (r != FFURL_EOK)
		return FFPARS_EBADVAL;

	if (ffurl_has(&u, FFURL_SCHEME)
		|| ffurl_has(&u, FFURL_PATH))
		return FFPARS_EBADVAL;

	ffstr host = ffurl_get(&u, val->ptr, FFURL_HOST);
	if (NULL == (net->conf.proxy.host = ffsz_alcopystr(&host)))
		return FFPARS_ESYS;
	net->conf.proxy.port = u.port;
	if (u.port == 0)
		net->conf.proxy.port = FFHTTP_PORT;

	return 0;
}

static int http_conf_done(ffparser_schem *p, void *obj)
{
	net->conf.buf_lowat = ffmin(net->conf.buf_lowat, net->conf.bufsize);
	return 0;
}


static void http_if_log(void *udata, uint level, const char *fmt, ...)
{
	uint lev;
	switch (level & 0x0f) {
	case FFHTTPCL_LOG_ERR:
		lev = FMED_LOG_ERR; break;
	case FFHTTPCL_LOG_WARN:
		lev = FMED_LOG_WARN; break;
	case FFHTTPCL_LOG_USER:
		lev = FMED_LOG_USER; break;
	case FFHTTPCL_LOG_INFO:
		lev = FMED_LOG_INFO; break;
	case FFHTTPCL_LOG_DEBUG:
		lev = FMED_LOG_DEBUG; break;
	default:
		return;
	}
	if (level & FFHTTPCL_LOG_SYS)
		lev |= FMED_LOG_SYS;

	va_list va;
	va_start(va, fmt);
	core->logv(lev, NULL, "net.httpif", fmt, va);
	va_end(va);
}

static void http_if_timer(fftmrq_entry *tmr, uint value_ms)
{
	core->timer(tmr, -(int)value_ms, 0);
}

static void* http_if_request(const char *method, const char *url, uint flags)
{
	void *c = ffhttpcl_request(method, url, flags);
	if (c == NULL)
		return NULL;

	struct ffhttpcl_conf conf;
	ffhttpcl_conf(c, &conf, FFHTTPCL_CONF_GET);
	conf.kq = core->kq;
	conf.log = &http_if_log;
	conf.timer = &http_if_timer;
	conf.nbuffers = net->conf.nbufs;
	conf.buffer_size = net->conf.bufsize;
	conf.connect_timeout = net->conf.conn_tmout;
	conf.timeout = net->conf.tmout;
	conf.max_redirect = net->conf.max_redirect;
	conf.max_reconnect = net->conf.max_reconnect;
	conf.debug_log = (core->loglev == FMED_LOG_DEBUG);
	ffhttpcl_conf(c, &conf, FFHTTPCL_CONF_SET);
	return c;
}
static void http_if_close(void *con)
{
	ffhttpcl_close(con);
}
static void http_if_sethandler(void *con, ffhttpcl_handler func, void *udata)
{
	ffhttpcl_sethandler(con, func, udata);
}
static void http_if_send(void *con, const ffstr *data)
{
	ffhttpcl_send(con, data);
}
static int http_if_recv(void *con, ffhttp_response **resp, ffstr *data)
{
	return ffhttpcl_recv(con, resp, data);
}
static void http_if_header(void *con, const ffstr *name, const ffstr *val, uint flags)
{
	ffhttpcl_header(con, name, val, flags);
}
static void http_if_conf(void *con, struct ffhttpcl_conf *conf, uint flags)
{
	ffhttpcl_conf(con, conf, flags);
}


#define FILT_NAME  "net.icy"

static const ffpars_arg icy_conf_args[] = {
	{ "meta",	FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(net_conf, meta) },
};

static int icy_config(ffpars_ctx *ctx)
{
	net->conf.meta = 1;
	ffpars_setargs(ctx, &net->conf, icy_conf_args, FFCNT(icy_conf_args));
	return 0;
}

static void* icy_open(fmed_filt *d)
{
	icy *c;
	if (NULL == (c = ffmem_new(icy)))
		return NULL;
	c->d = d;

	int v = net->track->getval(d->trk, "out-copy");
	c->out_copy = (v != FMED_NULL);
	c->save_oncmd = (v == FMED_OUTCP_CMD);

	const char *s = net->track->getvalstr(d->trk, "icy_format");
	ffstr_setz(&c->next_filt_ext, s);

	return c;
}

static void icy_close(void *ctx)
{
	icy *c = ctx;
	ffstr_free(&c->artist);
	ffstr_free(&c->title);

	if (c->netin != NULL) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	ffmem_free(c);
}

/** Initialize after a new connection is established. */
static int icy_reset(icy *c, fmed_filt *d)
{
	fficy_parseinit(&c->icy, fmed_getval("icy_meta_int"));

	// "netin" may be initialized if we've reconnected after I/O failure
	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c);
	return FMED_RDATA;
}

static int icy_process(void *ctx, fmed_filt *d)
{
	icy *c = ctx;
	int r;
	ffstr s;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		if (d->net_reconnect) {
			d->net_reconnect = 0;
			if (FMED_RDATA != (r = icy_reset(c, d)))
				return r;
		}
		ffstr_set(&c->data, d->data, d->datalen);
		d->datalen = 0;
	}

	for (;;) {
		if (c->data.len == 0)
			return FMED_RMORE;

		size_t n = c->data.len;
		r = fficy_parse(&c->icy, c->data.ptr, &n, &s);
		ffstr_shift(&c->data, n);
		switch (r) {
		case FFICY_RDATA:
			if (c->netin != NULL) {
				netin_write(c->netin, &s);
			}

			d->out = s.ptr;
			d->outlen = s.len;
			return FMED_RDATA;

		// case FFICY_RMETACHUNK:

		case FFICY_RMETA:
			icy_setmeta(c, &s);
			break;
		}
	}
}

static int icy_setmeta(icy *c, const ffstr *_data)
{
	fficymeta icymeta;
	ffstr artist = {0}, title = {0}, data = *_data, pair[2];
	fmed_que_entry *qent;
	int r;
	ffbool istitle = 0;

	dbglog(c->d->trk, "meta: [%L] %S", data.len, &data);
	fficy_metaparse_init(&icymeta);

	while (data.len != 0) {
		size_t n = data.len;
		r = fficy_metaparse(&icymeta, data.ptr, &n);
		ffstr_shift(&data, n);

		switch (r) {
		case FFPARS_KEY:
			if (ffstr_eqcz(&icymeta.val, "StreamTitle"))
				istitle = 1;
			break;

		case FFPARS_VAL:
			if (istitle) {
				fficy_streamtitle(icymeta.val.ptr, icymeta.val.len, &artist, &title);
				data.len = 0;
			}
			break;

		default:
			errlog(c->d->trk, "bad metadata");
			data.len = 0;
			break;
		}
	}

	qent = (void*)c->d->track->getval(c->d->trk, "queue_item");
	ffarr utf = {0};

	if (ffutf8_valid(artist.ptr, artist.len))
		ffarr_append(&utf, artist.ptr, artist.len);
	else
		ffutf8_strencode(&utf, artist.ptr, artist.len, FFU_WIN1252);
	ffstr_free(&c->artist);
	ffstr_acqstr3(&c->artist, &utf);

	ffarr_null(&utf);
	if (ffutf8_valid(title.ptr, title.len))
		ffarr_append(&utf, title.ptr, title.len);
	else
		ffutf8_strencode(&utf, title.ptr, title.len, FFU_WIN1252);
	ffstr_free(&c->title);
	ffstr_acqstr3(&c->title, &utf);

	ffstr_setcz(&pair[0], "artist");
	ffstr_set2(&pair[1], &c->artist);
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);

	ffstr_setcz(&pair[0], "title");
	ffstr_set2(&pair[1], &c->title);
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);

	c->d->meta_changed = 1;

	if (c->netin != NULL && c->netin->fn_dyn) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c);
	return 0;
}

#undef FILT_NAME


enum { IN_WAIT = 1, IN_DATANEXT };

static void* netin_create(icy *c)
{
	netin *n;
	void *trk;
	fmed_trk *trkconf;
	if (NULL == (n = ffmem_tcalloc1(netin)))
		goto fail;

	if (NULL == (trk = net->track->create(FMED_TRK_TYPE_NETIN, "")))
		goto fail;

	if (0 == net->track->cmd(trk, FMED_TRACK_FILT_ADDLAST, "net.in"))
		goto fail;

	const fmed_modinfo *mi = core->getmod2(FMED_MOD_INEXT, c->next_filt_ext.ptr, c->next_filt_ext.len);
	if (mi == NULL)
		goto fail;
	if (0 == net->track->cmd(trk, FMED_TRACK_FILT_ADDLAST, mi->name))
		goto fail;

	trkconf = net->track->conf(trk);
	trkconf->out_overwrite = c->d->out_overwrite;

	const char *output;
	output = net->track->getvalstr(c->d->trk, "out_filename");
	n->fn_dyn = (NULL != ffsz_findc(output, '$'));
	net->track->setvalstr(trk, "output", output);

	net->track->setval(trk, "netin_ptr", (size_t)n);
	n->c = c;

	if (c->save_oncmd) {
		net->track->setval(trk, "out_bufsize", 2 * 1024 * 1024);
		trkconf->out_file_del = 1;
	}

	if (1 == net->track->getval(c->d->trk, "out_stream_copy"))
		trkconf->stream_copy = 1;

	net->track->setvalstr4(trk, "artist", (void*)&c->artist, FMED_TRK_META | FMED_TRK_VALSTR);
	net->track->setvalstr4(trk, "title", (void*)&c->title, FMED_TRK_META | FMED_TRK_VALSTR);

	c->d->track->cmd2(trk, FMED_TRACK_META_COPYFROM, c->d->trk);

	net->track->cmd(trk, FMED_TRACK_START);
	return n;

fail:
	ffmem_free(n);
	return NULL;
}

static void netin_write(netin *n, const ffstr *data)
{
	if (data == NULL) {
		n->fin = 1;
		n->c = NULL;
	} else
		ffarr_append(&n->d[n->idx], data->ptr, data->len);
	if (n->state == IN_WAIT)
		net->track->cmd(n->trk, FMED_TRACK_WAKE);
}

static void* netin_open(fmed_filt *d)
{
	netin *n;
	n = (void*)fmed_getval("netin_ptr");
	n->trk = d->trk;
	n->state = IN_DATANEXT;
	return n;
}

static void netin_close(void *ctx)
{
	netin *n = ctx;
	ffarr_free(&n->d[0]);
	ffarr_free(&n->d[1]);
	if (n->c != NULL && n->c->netin == n)
		n->c->netin = NULL;
	ffmem_free(n);
}

static int netin_process(void *ctx, fmed_filt *d)
{
	netin *n = ctx;
	switch (n->state) {
	case IN_DATANEXT:
		if (n->d[n->idx].len == 0 && !n->fin) {
			n->state = IN_WAIT;
			return FMED_RASYNC;
		}
		break;

	case IN_WAIT:
		break;
	}
	n->state = IN_DATANEXT;
	d->out = n->d[n->idx].ptr,  d->outlen = n->d[n->idx].len;
	n->d[n->idx].len = 0;
	n->idx = ffint_cycleinc(n->idx, 2);
	if (n->fin)
		return FMED_RDONE;

	// get cmd from master track
	if (n->c->save_oncmd && n->c->d->save_trk) {
		n->c->d->save_trk = 0;
		d->out_file_del = 0;
	}

	return FMED_RDATA;
}


#define FILT_NAME  "net.httpcli"

struct httpclient {
	void *con;
	void *trk;
	uint st;
	uint status;
	ffstr data;
	ffstr next_filt_ext;
	fmed_filt *d;
};

static void* httpcli_open(fmed_filt *d)
{
	const char *url = d->track->getvalstr(d->trk, "input");
	ffstr ext;
	ffpath_split3(url, ffsz_len(url), NULL, NULL, &ext);
	if (ffstr_eqz(&ext, "m3u8")) {
		if (0 == net->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "net.hls"))
			return NULL;
		return FMED_FILT_SKIP;
	}

	struct httpclient *c;
	if (NULL == (c = ffmem_new(struct httpclient)))
		return NULL;
	c->trk = d->trk;
	c->d = d;
	return c;
}

static void httpcli_close(void *ctx)
{
	struct httpclient *c = ctx;
	http_iface.close(c->con);
	ffmem_free(c);
}

/** Process response: add appropriate filters to the chain */
static int httpcli_resp(struct httpclient *c, ffhttp_response *resp)
{
	if (resp->code != 200) {
		ffstr ln = ffhttp_respstatus(resp);
		errlog(c->trk, "resource unavailable: %S", &ln);
		return FMED_RERR;
	}

	ffstr s, ext;
	if (0 != ffhttp_findihdr(&resp->h, FFHTTP_CONTENT_TYPE, &s)) {
		if (ffstr_ieqz(&s, "audio/mpeg"))
			ffstr_setz(&ext, "mp3");
		else if (ffstr_ieqz(&s, "audio/aac") || ffstr_ieqz(&s, "audio/aacp"))
			ffstr_setz(&ext, "aac");
		else if (ffstr_ieqz(&s, "application/ogg"))
			ffstr_setz(&ext, "ogg");
		else {
			errlog(c->trk, "unsupported Content-Type: %S", &s);
			return FMED_RERR;
		}
	} else {
		ffstr_setz(&ext, "mp3");
		dbglog(c->trk, "no Content-Type HTTP header in response, assuming MPEG");
	}

	if (c->next_filt_ext.len != 0) {

		if (!ffstr_eq2(&c->next_filt_ext, &ext)) {
			errlog(c->trk, "unsupported behaviour: stream is changing audio format from %S to %S"
				, &c->next_filt_ext, &ext);
			return FMED_RERR;
		}
		return FMED_RDATA;
	}

	const fmed_modinfo *mi;
	if (NULL == (mi = core->getmod2(FMED_MOD_INEXT, ext.ptr, ext.len))) {
		errlog(c->trk, "no module configured to open .%S stream", &ext);
		return FMED_RERR;
	}
	if (0 == net->track->cmd(c->trk, FMED_TRACK_FILT_ADD, mi->name))
		return FMED_RERR;
	c->next_filt_ext = ext;

	s = fficy_shdr[FFICY_HMETAINT];
	if (0 != ffhttp_findhdr(&resp->h, s.ptr, s.len, &s)) {
		uint n;
		if (ffstr_toint(&s, &n, FFS_INT32)) {
			if (0 == net->track->cmd(c->trk, FMED_TRACK_FILT_ADD, "net.icy"))
				return FMED_RERR;
			net->track->setvalstr(c->trk, "icy_format", ext.ptr);
			net->track->setval(c->trk, "icy_meta_int", n);
		} else {
			warnlog(c->trk, "invalid value for HTTP response header %S: %S"
				, &fficy_shdr[FFICY_HMETAINT], &s);
		}
	}


	return FMED_RDATA;
}

/** Handle events from 'httpif'. */
static void httpcli_handler(void *param)
{
	struct httpclient *c = param;
	ffhttp_response *resp;
	ffstr data;
	int r = http_iface.recv(c->con, &resp, &data);
	c->status = r;

	switch (r) {

	case FFHTTPCL_RESP:
		c->d->net_reconnect = 1;
		r = httpcli_resp(c, resp);
		if (r == FMED_RERR) {
			c->st = 3;
			net->track->cmd(c->trk, FMED_TRACK_WAKE);
			return;
		}
		break;

	case FFHTTPCL_RESP_RECV:
		c->data = data;
		//fallthrough
	case FFHTTPCL_DONE:
		net->track->cmd(c->trk, FMED_TRACK_WAKE);
		return;
	}

	if (r < 0) {
		net->track->cmd(c->trk, FMED_TRACK_WAKE);
		return;
	}

	http_iface.send(c->con, NULL);
}

static void httpcli_log(void *udata, uint level, const char *fmt, ...)
{
	struct httpclient *c = udata;

	uint lev;
	switch (level & 0x0f) {
	case FFHTTPCL_LOG_ERR:
		lev = FMED_LOG_ERR; break;
	case FFHTTPCL_LOG_WARN:
		lev = FMED_LOG_WARN; break;
	case FFHTTPCL_LOG_USER:
		lev = FMED_LOG_USER; break;
	case FFHTTPCL_LOG_INFO:
		lev = FMED_LOG_INFO; break;
	case FFHTTPCL_LOG_DEBUG:
		lev = FMED_LOG_DEBUG; break;
	default:
		return;
	}
	if (level & FFHTTPCL_LOG_SYS)
		lev |= FMED_LOG_SYS;

	va_list va;
	va_start(va, fmt);
	core->logv(lev, c->d->trk, "net.httpif", fmt, va);
	va_end(va);
}

/**
Make request via 'httpif'.
Get data from 'httpif' and pass further through the chain. */
static int httpcli_process(void *ctx, fmed_filt *d)
{
	struct httpclient *c = ctx;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	switch (c->st) {
	case 0: {
		const char *url = d->track->getvalstr(d->trk, "input");
		if (NULL == (c->con = http_iface.request("GET", url, 0)))
			return FMED_RERR;

		struct ffhttpcl_conf conf;
		http_iface.conf(c->con, &conf, FFHTTPCL_CONF_GET);
		if (d->trk != NULL)
			conf.kq = (fffd)net->track->cmd(d->trk, FMED_TRACK_KQ);
		conf.log = &httpcli_log;
		http_iface.conf(c->con, &conf, FFHTTPCL_CONF_SET);

		core->getmod("net.icy"); // load net.icy config
		if (net->conf.meta) {
			ffstr val;
			ffstr_setz(&val, "1");
			http_iface.header(c->con, &fficy_shdr[FFICY_HMETADATA], &val, 0);
		}

		if (net->conf.user_agent != 0) {
			ffstr s;
			ffstr_setz(&s, http_ua[net->conf.user_agent - 1]);
			http_iface.header(c->con, &ffhttp_shdr[FFHTTP_USERAGENT], &s, 0);
		}

		http_iface.sethandler(c->con, &httpcli_handler, c);
		c->st = 1;
		http_iface.send(c->con, NULL);
		return FMED_RASYNC;
	}

	case 1:
		switch (c->status) {
		case FFHTTPCL_RESP_RECV:
			c->st = 2;
			d->out = c->data.ptr,  d->outlen = c->data.len;
			return FMED_RDATA;

		case FFHTTPCL_DONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFHTTPCL_ENOADDR:
			c->d->e_no_source = 1;
			break;
		}
		return FMED_RERR;

	case 2:
		c->st = 1;
		http_iface.send(c->con, NULL);
		return FMED_RASYNC;

	case 3:
		return FMED_RERR;
	}

	return FMED_RERR;
}

#undef FILT_NAME
