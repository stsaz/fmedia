/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <core-cmd.h>

#include <FF/path.h>
#include <FF/data/conf.h>
#include <FF/data/psarg.h>
#include <FF/gui/winapi.h>
#include <FFOS/process.h>
#include <FFOS/mem.h>


static fmed_cmd *fmed;
static fmed_core *core;

FF_IMP fmed_core* core_init(fmed_cmd **ptr);
FF_IMP void core_free(void);

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

	ffui_msgdlg_show("fmedia " FMED_VER, buf, s - buf, FFUI_MSGDLG_ERR);
}


static void open_input(void)
{
	const fmed_queue *qu;
	fmed_que_entry e;
	const ffsyschar *w = GetCommandLine();
	char *s;
	ffstr args, fn;
	size_t n;
	ffbool skip = 1, added = 0;

	if (NULL == (qu = core->getmod("#queue.queue")))
		return;

	if (NULL == (s = ffsz_alcopyqz(w)))
		return;
	ffstr_setz(&args, s);

	while (args.len != 0) {
		n = ffstr_nextval(args.ptr, args.len, &fn, ' ' | FFSTR_NV_DBLQUOT);
		ffstr_shift(&args, n);

		if (skip) {
			skip = 0;
			continue;
		}

		if (fn.len != 0) {
			ffmem_tzero(&e);
			e.url = fn;
			qu->add(&e);
			added = 1;
		}
	}

	ffmem_free(s);

	if (added)
		qu->cmd(FMED_QUE_PLAY, NULL);
}


/**
@mode: enum FMED_INSTANCE_MODE */
static int gcmd_send(const fmed_globcmd_iface *globcmd, uint mode)
{
	int r = -1;
	const char *fn;
	ffconfw confw;
	ffpsarg a;

	ffmem_tzero(&confw);
	ffpsarg_init(&a, NULL, 0);

	ffpsarg_next(&a);

	if (mode == FMED_IM_CLEARPLAY)
		if (0 != ffconf_write(&confw, FFSTR("clear"), FFCONF_TKEY))
			goto end;

	ffstr cmd;
	if (mode == FMED_IM_ADD)
		ffstr_setz(&cmd, "add");
	else
		ffstr_setz(&cmd, "play");
	if (0 != ffconf_write(&confw, cmd.ptr, cmd.len, FFCONF_TKEY))
		goto end;

	while (NULL != (fn = ffpsarg_next(&a))) {
		if (0 != ffconf_write(&confw, fn, ffsz_len(fn), FFCONF_TVAL))
			goto end;
	}

	if (0 != ffconf_write(&confw, NULL, 0, FFCONF_TKEY))
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


int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	ffmem_init();

	if (NULL == (core = core_init(&fmed)))
		return 1;

	{
	char fn[FF_MAXPATH];
	ffstr path;
	const char *p = ffps_filename(fn, sizeof(fn), NULL);
	if (p == NULL)
		return 1;
	ffpath_split2(p, ffsz_len(p), &path, NULL);
	if (NULL == ffstr_copy(&fmed->root, path.ptr, path.len + FFSLEN("/")))
		return 1;
	}

	fmed->log = &fgui_logger;
	fmed->gui = 1;
	if (0 != core->sig(FMED_CONF))
		goto end;

	if (NULL == (fmed->log = core->getmod("gui.log")))
		goto end;

	int im = core->getval("instance_mode");
	const fmed_globcmd_iface *globcmd = NULL;
	if (im != FMED_IM_OFF
		&& NULL != (globcmd = core->getmod("#globcmd.globcmd"))
		&& 0 == globcmd->ctl(FMED_GLOBCMD_OPEN)) {

		gcmd_send(globcmd, im);
		goto end;
	}

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (globcmd != NULL) {
		globcmd->ctl(FMED_GLOBCMD_START);
	}

	open_input();

	core->sig(FMED_START);

end:
	core_free();
	return 0;
}
