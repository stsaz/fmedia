/** Global commands.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <util/conf2.h>


static const fmed_core *core;

// FMEDIA MODULE
static const void* globcmd_iface(const char *name);
static int globcmd_conf(const char *name, fmed_conf_ctx *ctx);
static int globcmd_sig(uint signo);
static void globcmd_destroy(void);
static const fmed_mod fmed_globcmd_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &globcmd_iface,
	.conf = &globcmd_conf,
	.sig = &globcmd_sig,
	.destroy = &globcmd_destroy,
};

// GLOBCMD IFACE
static int globcmd_ctl(uint cmd, ...);
static int globcmd_write(const void *data, size_t len);
static const fmed_globcmd_iface fmed_globcmd = {
	&globcmd_ctl, &globcmd_write
};

enum {
	GCMD_PIPE_IN_BUFSIZE = 1028,
};

typedef struct globcmd {
	ffkevent kev;
	fffd lpipe;
	ffkq_task accept_task;
	fffd opened_fd;
	ffarr pipename_full;
	char *pipe_name;
	const fmed_track *track;
} globcmd;

static globcmd *g;

static const fmed_conf_arg globcmd_conf_args[] = {
	{ "pipe_name",  FMC_STRZNE,  FMC_O(globcmd, pipe_name) },
	{}
};

static int globcmd_init(void);
static int globcmd_prep(const char *pipename);
static void globcmd_free(void);
static int globcmd_listen(void);
static void globcmd_accept(void *udata);
static int globcmd_accept1(void);
static void globcmd_onaccept(fffd peer);

typedef struct cmd_parser {
	ffconf conf;
	const fmed_queue *qu;
	const fmed_que_entry *first;
} cmd_parser;

static int globcmd_parse(cmd_parser *c, const ffstr *in);


const fmed_mod* fmed_getmod_globcmd(const fmed_core *_core)
{
	core = _core;
	return &fmed_globcmd_mod;
}


static const void* globcmd_iface(const char *name)
{
	if (!ffsz_cmp(name, "globcmd"))
		return &fmed_globcmd;
	return NULL;
}

static int globcmd_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "globcmd")) {
		g->pipe_name = ffsz_alcopyz("fmedia");
		fmed_conf_addctx(ctx, g, globcmd_conf_args);
		return 0;
	}
	return -1;
}

static int globcmd_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		if (0 != globcmd_init())
			return -1;
		break;
	}
	return 0;
}

static void globcmd_destroy(void)
{
	globcmd_free();
}


static int globcmd_ctl(uint cmd, ...)
{
	int r = -1;
	va_list va;
	va_start(va, cmd);
	switch (cmd) {
	case FMED_GLOBCMD_START: {
		const char *pipename = va_arg(va, void*);
		if (0 != globcmd_prep(pipename))
			goto end;
		if (0 != globcmd_listen())
			goto end;
		if (NULL == (g->track = core->getmod("#core.track")))
			goto end;
		r = 0;
		break;
	}

	case FMED_GLOBCMD_OPEN: {
		const char *pipename = va_arg(va, void*);
		if (0 != globcmd_prep(pipename))
			goto end;
		if (FF_BADFD == (g->opened_fd = ffpipe_connect(g->pipename_full.ptr))) {
			syserrlog(core, NULL, "globcmd", "pipe connect: %s", g->pipename_full.ptr);
			goto end;
		}
		dbglog(core, NULL, "globcmd", "connected");
		r = 0;
		break;
	}
	}

end:
	va_end(va);
	return r;
}

static int globcmd_write(const void *data, size_t len)
{
	if (len != (size_t)fffile_write(g->opened_fd, data, len))
		return -1;
	dbglog(core, NULL, "globcmd", "written %L bytes", len);
	return 0;
}


static int globcmd_init(void)
{
	if (NULL == (g = ffmem_tcalloc1(globcmd)))
		return -1;
	g->opened_fd = FFPIPE_NULL;
	g->lpipe = FFPIPE_NULL;
	ffkev_init(&g->kev);
	return 0;
}

/** Prepare name of pipe. */
static int globcmd_prep(const char *pipename)
{
	if (g->pipename_full.len != 0)
		return 0;
	if (pipename == NULL)
		pipename = g->pipe_name;
#ifdef FF_UNIX
	if (0 == ffstr_catfmt(&g->pipename_full, "/tmp/.%s-unix-%u%Z", pipename, getuid()))
		return -1;
#else
	if (0 == ffstr_catfmt(&g->pipename_full, "\\\\.\\pipe\\%s%Z", pipename))
		return -1;
#endif
	ffmem_safefree0(g->pipe_name);
	return 0;
}

static void globcmd_free(void)
{
	if (g->lpipe != FFPIPE_NULL) {
		ffpipe_close(g->lpipe);
#ifdef FF_UNIX
		fffile_rm(g->pipename_full.ptr);
#endif
	}
	FF_SAFECLOSE(g->opened_fd, FF_BADFD, ffpipe_client_close);
	ffarr_free(&g->pipename_full);
	ffmem_safefree0(g->pipe_name);
	ffmem_free0(g);
}

static int globcmd_listen(void)
{
	if (FFPIPE_NULL == (g->lpipe = ffpipe_create_named(g->pipename_full.ptr, FFPIPE_ASYNC))) {
		syserrlog(core, NULL, "globcmd", "pipe create: %s", g->pipename_full.ptr);
		goto end;
	}
	dbglog(core, NULL, "globcmd", "created pipe: %s", g->pipename_full.ptr);

	g->kev.udata = g;
	g->kev.oneshot = 0;
	if (0 != ffkq_attach(core->kq, g->lpipe, ffkev_ptr(&g->kev), FFKQ_READ)) {
		syserrlog(core, NULL, "globcmd", "%s", "pipe kq attach");
		goto end;
	}

	globcmd_accept(NULL);
	return 0;

end:
	FF_SAFECLOSE(g->lpipe, FFPIPE_NULL, ffpipe_close);
	return -1;
}

static void globcmd_accept(void *udata)
{
	for (;;) {
		if (0 != globcmd_accept1())
			break;
	}
}

static int globcmd_accept1(void)
{
	fffd peer = ffpipe_accept_async(g->lpipe, &g->accept_task);
	if (peer == FFPIPE_NULL) {
		if (fferr_last() == FFPIPE_EINPROGRESS) {
			dbglog(core, NULL, "globcmd", "listening");
			g->kev.handler = globcmd_accept;
		} else {
			syserrlog(core, NULL, "globcmd", "pipe accept: %s", g->pipename_full.ptr);
		}
		return -1;
	}
	globcmd_onaccept(peer);
	ffpipe_peer_close(peer);
	return 0;
}

static void globcmd_onaccept(fffd peer)
{
	ffarr buf = {0};
	ffstr in;
	ssize_t r;
	cmd_parser c = {};

	dbglog(core, NULL, "globcmd", "accepted client");

	c.qu = core->getmod("#queue.queue");
	ffconf_init(&c.conf);

	if (NULL == ffarr_alloc(&buf, GCMD_PIPE_IN_BUFSIZE)) {
		syserrlog(core, NULL, "globcmd", "mem alloc", 0);
		goto done;
	}

	for (;;) {
		r = ffpipe_read(peer, buf.ptr, buf.cap);
		if (r < 0) {
			syserrlog(core, NULL, "globcmd", "%s", fffile_read_S);
			break;
		} else if (r == 0) {
			ffstr_setcz(&in, "\n");
			r = globcmd_parse(&c, &in);
			if (r < 0)
				goto done;
			break;
		}
		ffstr_set(&in, buf.ptr, r);
		dbglog(core, NULL, "globcmd", "read %L bytes", r);

		r = globcmd_parse(&c, &in);
		if (r < 0)
			goto done;
	}

done:
	ffarr_free(&buf);
	ffconf_fin(&c.conf);
	dbglog(core, NULL, "globcmd", "done with client");
}

enum CMD {
	CMD_ADD,
	CMD_CLEAR,
	CMD_NEXT,
	CMD_PAUSE,
	CMD_PLAY,
	CMD_QUIT,
	CMD_STOP,
	CMD_UNPAUSE,
};

static const char* const cmds_sorted_str[] = {
	"add", // "add INPUT..."
	"clear",
	"next",
	"pause",
	"play", // "play INPUT..."
	"quit",
	"stop",
	"unpause",
};

static void exec_cmd(cmd_parser *c, int cmd, ffstr *val)
{
	switch ((enum CMD)cmd) {
	case CMD_CLEAR:
		c->qu->cmd(FMED_QUE_CLEAR, NULL);
		break;

	case CMD_NEXT:
		c->qu->cmd(FMED_QUE_NEXT2, NULL);
		break;

	case CMD_ADD:
	case CMD_PLAY: {
		if (val == NULL)
			break;
		fmed_que_entry e = {}, *ent;
		e.url = *val;
		ent = c->qu->add(&e);
		if (cmd == CMD_PLAY)
			c->qu->cmd(FMED_QUE_PLAY_EXCL, (void*)ent);
		break;
	}

	case CMD_PAUSE:
		g->track->cmd((void*)-1, FMED_TRACK_PAUSE);
		break;

	case CMD_UNPAUSE:
		g->track->cmd((void*)-1, FMED_TRACK_UNPAUSE);
		break;

	case CMD_STOP:
		g->track->cmd((void*)-1, FMED_TRACK_STOPALL);
		break;

	case CMD_QUIT:
		g->track->cmd((void*)-1, FMED_TRACK_STOPALL_EXIT);
		break;
	}
}

/** Parse commands.  Format:
CMD [PARAMS] \n
...
*/
static int globcmd_parse(cmd_parser *c, const ffstr *in)
{
	ffstr data = *in;
	int r, cmd;

	for (;;) {

		ffstr val;
		r = ffconf_parse3(&c->conf, &data, &val);

		switch (r) {
		case FFCONF_RMORE:
			return 0;

		case FFCONF_RKEY:
			r = ffszarr_findsorted(cmds_sorted_str, FFCNT(cmds_sorted_str), val.ptr, val.len);
			if (r < 0) {
				warnlog(core, NULL, "globcmd", "unsupported command: %S", &val);
				return -1;
			}
			dbglog(core, NULL, "globcmd", "received pipe command: %S", &val);
			cmd = r;
			exec_cmd(c, r, NULL);
			break;

		case FFCONF_RVAL:
		case FFCONF_RVAL_NEXT:
			exec_cmd(c, cmd, &val);
			break;

		default:
			warnlog(core, NULL, "globcmd", "pipe command parse: (%d) %s", r, ffconf_errstr(r));
			return -1;
		}
	}

	return 0;
}
