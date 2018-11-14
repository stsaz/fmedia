/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <core-cmd.h>

#include <FF/path.h>
#include <FF/data/conf.h>
#include <FF/data/psarg.h>
#include <FF/gui/winapi.h>
#include <FFOS/process.h>
#include <FFOS/mem.h>
#include <FFOS/sig.h>


struct gctx {
	ffdl core_dl;
	fmed_core* (*core_init)(fmed_cmd **ptr, char **argv, char **env);
	void (*core_free)(void);
};
static struct gctx *g;
static fmed_cmd *gcmd;
static fmed_core *core;


//LOG
static void fgui_log(uint flags, fmed_logdata *ld);
static const fmed_log fgui_logger = {
	&fgui_log
};

static int gcmd_send(const fmed_globcmd_iface *globcmd, uint mode);

static void open_input(void);


static void fgui_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + sizeof(buf) - FFSLEN("\r\n");

	s += ffs_fmt(s, end, "%s %s %s: ", ld->stime, ld->level, ld->module);
	if (ld->ctx != NULL)
		s += ffs_fmt(s, end, "%S:\t", ld->ctx);
	s += ffs_fmtv(s, end, ld->fmt, ld->va);
	if (flags & FMED_LOG_SYS)
		s += ffs_fmt(s, end, ": %E", fferr_last());
	*s++ = '\r';
	*s++ = '\n';

	ffui_msgdlg_show("fmedia", buf, s - buf, FFUI_MSGDLG_ERR);
}


static void open_input(void)
{
	const fmed_queue *qu;
	fmed_que_entry e;
	const char *fn;
	void *qe, *first = NULL;
	ffpsarg a;

	if (NULL == (qu = core->getmod("#queue.queue")))
		return;

	ffpsarg_init(&a, NULL, 0);
	ffpsarg_next(&a);

	while (NULL != (fn = ffpsarg_next(&a))) {
		ffmem_tzero(&e);
		ffstr_setz(&e.url, fn);
		qe = qu->add(&e);
		if (first == NULL)
			first = qe;
	}

	ffpsarg_destroy(&a);

	if (first != NULL)
		qu->cmd(FMED_QUE_PLAY, first);
}


/**
@mode: enum FMED_INSTANCE_MODE */
static int gcmd_send(const fmed_globcmd_iface *globcmd, uint mode)
{
	int r = -1;
	const char *fn;
	ffconfw confw;
	ffpsarg a;

	ffconf_winit(&confw, NULL, 0);
	ffpsarg_init(&a, NULL, 0);

	ffpsarg_next(&a);

	if (mode == FMED_IM_CLEARPLAY)
		ffconf_write(&confw, FFSTR("clear"), FFCONF_TKEY);

	ffstr cmd;
	if (mode == FMED_IM_ADD)
		ffstr_setz(&cmd, "add");
	else
		ffstr_setz(&cmd, "play");
	ffconf_write(&confw, cmd.ptr, cmd.len, FFCONF_TKEY);

	while (NULL != (fn = ffpsarg_next(&a))) {
		ffconf_writez(&confw, fn, FFCONF_TVAL);
	}

	if (0 == ffconf_write(&confw, NULL, 0, FFCONF_FIN))
		goto end;

	if (0 != globcmd->write(confw.buf.ptr, confw.buf.len)) {
		goto end;
	}

	r = 0;
end:
	ffconf_wdestroy(&confw);
	ffpsarg_destroy(&a);
	return r;
}


static int loadcore(char *argv0)
{
	int rc = -1;
	char buf[FF_MAXPATH];
	const char *path;
	ffdl dl = NULL;
	ffarr a = {0};

	if (NULL == (path = ffps_filename(buf, sizeof(buf), argv0)))
		goto end;
	if (0 == ffstr_catfmt(&a, "%s/../mod/core.%s%Z", path, FFDL_EXT))
		goto end;
	a.len = ffpath_norm(a.ptr, a.cap, a.ptr, a.len - 1, 0);
	a.ptr[a.len] = '\0';

	if (NULL == (dl = ffdl_open(a.ptr, 0))) {
		fffile_fmt(ffstderr, NULL, "can't load %s: %s\n", a.ptr, ffdl_errstr());
		goto end;
	}

	g->core_init = (void*)ffdl_addr(dl, "core_init");
	g->core_free = (void*)ffdl_addr(dl, "core_free");
	if (g->core_init == NULL || g->core_free == NULL)
		goto end;

	g->core_dl = dl;
	dl = NULL;
	rc = 0;

end:
	FF_SAFECLOSE(dl, NULL, ffdl_close);
	ffarr_free(&a);
	return rc;
}

#ifndef _DEBUG
extern void _crash_handler(const char *fullname, struct ffsig_info *inf);

/** Called by FFOS on program crash. */
static void crash_handler(struct ffsig_info *inf)
{
	_crash_handler("fmedia", inf);
}
#endif

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	char *argv[1] = { NULL };
	ffmem_init();
	if (NULL == (g = ffmem_new(struct gctx)))
		return 1;

#ifndef _DEBUG
	static const uint sigs_fault[] = { FFSIG_SEGV, FFSIG_ILL, FFSIG_FPE };
	ffsig_subscribe(&crash_handler, sigs_fault, FFCNT(sigs_fault));
	// ffsig_raise(FFSIG_SEGV);
#endif

	if (0 != loadcore(NULL))
		goto end;

	if (NULL == (core = g->core_init(&gcmd, argv, NULL)))
		goto end;

	gcmd->log = &fgui_logger;
	gcmd->gui = 1;
	if (0 != core->cmd(FMED_CONF, NULL))
		goto end;

	if (NULL == (gcmd->log = core->getmod("gui.log")))
		goto end;

	int im = core->getval("instance_mode");
	const fmed_globcmd_iface *globcmd = NULL;
	if (im != FMED_IM_OFF
		&& NULL != (globcmd = core->getmod("#globcmd.globcmd"))
		&& 0 == globcmd->ctl(FMED_GLOBCMD_OPEN, NULL)) {

		gcmd_send(globcmd, im);
		goto end;
	}

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (globcmd != NULL) {
		globcmd->ctl(FMED_GLOBCMD_START, NULL);
	}

	open_input();

	core->sig(FMED_START);

end:
	if (core != NULL) {
		g->core_free();
	}
	FF_SAFECLOSE(g->core_dl, NULL, ffdl_close);
	ffmem_free(g);
	return 0;
}
