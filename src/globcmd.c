/** Global commands.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <FF/data/conf.h>
#include <FFOS/asyncio.h>


static const fmed_core *core;

// FMEDIA MODULE
static const void* globcmd_iface(const char *name);
static int globcmd_conf(const char *name, ffpars_ctx *ctx);
static int globcmd_sig(uint signo);
static void globcmd_destroy(void);
static const fmed_mod fmed_globcmd_mod = {
	.iface = &globcmd_iface,
	.conf = &globcmd_conf,
	.sig = &globcmd_sig,
	.destroy = &globcmd_destroy,
};

// GLOBCMD IFACE
static int globcmd_ctl(uint cmd);
static int globcmd_write(const void *data, size_t len);
static const fmed_globcmd_iface fmed_globcmd = {
	&globcmd_ctl, &globcmd_write
};

enum {
	GCMD_PIPE_IN_BUFSIZE = 1028,
};

typedef struct globcmd {
	ffkevent kev;
	fffd opened_fd;
	ffarr pipename_full;
	char *pipe_name;
} globcmd;

static globcmd *g;

static const ffpars_arg globcmd_conf_args[] = {
	{ "pipe_name",  FFPARS_TCHARPTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY,  FFPARS_DSTOFF(globcmd, pipe_name) },
};

static int globcmd_init(void);
static int globcmd_prep(void);
static void globcmd_free(void);
static int globcmd_listen(void);
static void globcmd_accept(void *udata);
static int globcmd_accept1(void);
static void globcmd_onaccept(fffd peer);

typedef struct cmd_parser {
	ffparser conf;
	uint cmd;
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

static int globcmd_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "globcmd")) {
		g->pipe_name = ffsz_alcopyz("fmedia");
		ffpars_setargs(ctx, g, globcmd_conf_args, FFCNT(globcmd_conf_args));
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


static int globcmd_ctl(uint cmd)
{
	switch (cmd) {
	case FMED_GLOBCMD_START:
		if (0 != globcmd_prep())
			goto end;
		if (0 != globcmd_listen())
			goto end;
		return 0;

	case FMED_GLOBCMD_OPEN:
		if (0 != globcmd_prep())
			goto end;
		if (FF_BADFD == (g->opened_fd = ffpipe_connect(g->pipename_full.ptr))) {
			syserrlog(core, NULL, "globcmd", "pipe connect: %s", g->pipename_full.ptr);
			goto end;
		}
		dbglog(core, NULL, "globcmd", "connected");
		return 0;
	}

end:
	return -1;
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
	g->opened_fd = FF_BADFD;
	ffkev_init(&g->kev);
	return 0;
}

static int globcmd_prep(void)
{
	if (g->pipename_full.len != 0)
		return 0;
#ifdef FF_UNIX
	if (0 == ffstr_catfmt(&g->pipename_full, "/tmp/.%s-unix-%u%Z", g->pipe_name, getuid()))
		return -1;
#else
	if (0 == ffstr_catfmt(&g->pipename_full, "\\\\.\\pipe\\%s%Z", g->pipe_name))
		return -1;
#endif
	ffmem_safefree0(g->pipe_name);
	return 0;
}

static void globcmd_free(void)
{
	if (g->kev.fd != FF_BADFD) {
		ffpipe_close(g->kev.fd);
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
	if (FF_BADFD == (g->kev.fd = ffpipe_create_named(g->pipename_full.ptr))) {
		syserrlog(core, NULL, "globcmd", "pipe create: %s", g->pipename_full.ptr);
		goto end;
	}
	dbglog(core, NULL, "globcmd", "created pipe: %s", g->pipename_full.ptr);

#ifdef FF_UNIX
	ffskt_nblock(g->kev.fd, 1);
#endif

	g->kev.udata = g;
	g->kev.oneshot = 0;
	if (0 != ffkev_attach(&g->kev, core->kq, FFKQU_READ)) {
		goto end;
	}

	globcmd_accept(NULL);
	return 0;

end:
	syserrlog(core, NULL, "core", "%s", "pipe create");
	FF_SAFECLOSE(g->kev.fd, FF_BADFD, ffpipe_close);
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
	fffd peer;
	peer = ffaio_pipe_accept(&g->kev, &globcmd_accept);
	if (peer == FF_BADFD) {
		if (!fferr_again(fferr_last()))
			syserrlog(core, NULL, "globcmd", "pipe accept: %s", g->pipename_full.ptr);
		dbglog(core, NULL, "globcmd", "listening");
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
	cmd_parser c;

	dbglog(core, NULL, "globcmd", "accepted client");

	ffmem_tzero(&c);
	c.qu = core->getmod("#queue.queue");
	ffmem_tzero(&c.conf);
	ffconf_parseinit(&c.conf);

	if (NULL == ffarr_alloc(&buf, GCMD_PIPE_IN_BUFSIZE)) {
		syserrlog(core, NULL, "globcmd", "single instance mode: %e", FFERR_BUFALOC);
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
	ffpars_free(&c.conf);
	dbglog(core, NULL, "globcmd", "done with client");
}

enum CMDS {
	CMD_ADD,
	CMD_CLEAR,
	CMD_PLAY,
	CMD_STOP,
};

static const char* const cmds_str[] = {
	"add", // "add INPUT..."
	"clear",
	"play", // "play INPUT..."
	"stop",
};

/** Parse commands.  Format:
CMD [PARAMS] \n
...
*/
static int globcmd_parse(cmd_parser *c, const ffstr *in)
{
	ffstr data = *in;
	int r;

	for (;;) {

		r = ffconf_parsestr(&c->conf, &data);
		if (ffpars_iserr(r)) {
			errlog(core, NULL, "globcmd", "pipe command parse: %s", ffpars_errstr(r));
			return -1;
		}
		if (r == FFPARS_MORE)
			return 0;

		ffstr val = c->conf.val;

		switch (c->conf.type) {

		case FFCONF_TKEY:
			r = ffszarr_findsorted(cmds_str, FFCNT(cmds_str), val.ptr, val.len);
			if (r < 0) {
				warnlog(core, NULL, "globcmd", "unsupported command: %S", &val);
				return -1;
			}
			dbglog(core, NULL, "globcmd", "received pipe command: %S", &val);
			c->cmd = r;

			switch (c->cmd) {
			case CMD_CLEAR:
				c->qu->cmd(FMED_QUE_CLEAR, NULL);
				break;

			case CMD_ADD:
				break;

			case CMD_STOP: {
				const fmed_track *track;
				if (NULL == (track = core->getmod("#core.track")))
					break;
				track->cmd((void*)-1, FMED_TRACK_STOPALL);
				break;
			}
			}
			break;

		case FFCONF_TVAL:
		case FFCONF_TVALNEXT:

			switch (c->cmd) {
			case CMD_ADD:
			case CMD_PLAY: {
				fmed_que_entry e, *ent;
				ffmem_tzero(&e);
				e.url = val;
				ent = c->qu->add(&e);
				if (c->cmd == CMD_PLAY)
					c->qu->cmd(FMED_QUE_PLAY_EXCL, (void*)ent);
				break;
			}
			}
			break;
		}
	}

	return 0;
}
