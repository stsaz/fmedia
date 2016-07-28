/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/path.h>
#include <FF/gui/winapi.h>
#include <FFOS/process.h>
#include <FFOS/mem.h>


static fmedia *fmed;
static fmed_core *core;

typedef struct inst_mode {
	int mode;
	ffarr pipename;
	ffkevent kev;
} inst_mode;
static inst_mode *imode;
#define IM_PIPE_NAME  "fmedia"


FF_IMP fmed_core* core_init(fmedia **ptr);
FF_IMP void core_free(void);

//LOG
static void fgui_log(uint flags, fmed_logdata *ld);
static const fmed_log fgui_logger = {
	&fgui_log
};

static int pipe_listen(const char *name, inst_mode *imode);
static void pipe_onaccept(void *udata);
static int pipe_add_inputfiles(fffd ph);

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


static int pipe_listen(const char *name, inst_mode *im)
{
	if (FF_BADFD == (im->kev.fd = ffpipe_create_named(name))) {
		goto end;
	}

	im->kev.udata = im;
	im->kev.oneshot = 0;
	if (0 != ffkev_attach(&im->kev, core->kq, FFKQU_READ)) {
		goto end;
	}

	if (0 != ffaio_pipe_listen(&im->kev, &pipe_onaccept))
		goto end;

	return 0;

end:
	FF_SAFECLOSE(im->kev.fd, FF_BADFD, ffpipe_close);
	return -1;
}

/**
Format: INPUT1\0 INPUT2\0 ... */
static void pipe_onaccept(void *udata)
{
	fmed_que_entry e, *first = NULL, *ent;
	ffarr buf;
	inst_mode *im = udata;
	ffbool skip = 0;

	const fmed_queue *qu = core->getmod("#queue.queue");

	if (im->mode == FMED_IM_CLEARPLAY)
		qu->cmd(FMED_QUE_CLEAR, NULL);

	if (NULL == ffarr_alloc(&buf, 4096)) {
		syserrlog(core, NULL, "gui", "single instance mode: %e", FFERR_BUFALOC);
		goto done;
	}

	for (;;) {
		ssize_t r = fffile_read(im->kev.fd, ffarr_end(&buf), ffarr_unused(&buf));
		if (r <= 0)
			break;
		buf.len += r;

		ffstr s;
		ffstr_set(&s, buf.ptr, buf.len);

		for (;;) {
			ffstr fn;
			fn.ptr = s.ptr;
			fn.len = ffsz_nlen(s.ptr, s.len);
			if (fn.len == s.len && (fn.len == 0 || ffarr_back(&s) != '\0'))
				break;

			ffstr_shift(&s, fn.len + 1);
			if (skip || fn.len == 0)
				continue;

			ffmem_tzero(&e);
			e.url = fn;
			ent = qu->add(&e);
			if (first == NULL)
				first = ent;
		}

		if (s.len == buf.cap) {
			warnlog(core, NULL, "gui", "single instance mode: %s", "skipping too long filename");
			skip = 1;
			buf.len = 0;
			continue;
		}
		_ffarr_rmleft(&buf, buf.len - s.len, sizeof(char));
	}

	ffarr_free(&buf);

	if (first == NULL) {
		const fmed_modinfo *gui;
		gui = core->getmod("gui");
		gui->m->sig(FMED_GUI_SHOW);

	} else if (im->mode == FMED_IM_PLAY || im->mode == FMED_IM_CLEARPLAY) {
		qu->cmd(FMED_QUE_PLAY_EXCL, first);
	}

done:
	ffpipe_disconnect(im->kev.fd);
	if (0 != ffaio_pipe_listen(&im->kev, &pipe_onaccept))
		syserrlog(core, NULL, "gui", "single instance mode: %s", "pipe listen");
}

static int pipe_add_inputfiles(fffd ph)
{
	int r = -1;
	ffbool skip = 1;
	char *cmd;
	ffstr args, fn;
	if (NULL == (cmd = ffsz_alcopyqz(GetCommandLine())))
		goto end;
	ffstr_setz(&args, cmd);

	while (args.len != 0) {
		size_t n = ffstr_nextval(args.ptr, args.len, &fn, ' ' | FFSTR_NV_DBLQUOT);
		ffstr_shift(&args, n);

		if (skip) {
			skip = 0;
			continue;
		}

		fn.len++;
		ffarr_back(&fn) = '\0';
		if (fn.len != (size_t)fffile_write(ph, fn.ptr, fn.len)) {
			goto end;
		}
	}

	r = 0;
end:
	ffmem_safefree(cmd);
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
	if (im != FMED_IM_OFF) {
		if (NULL == (imode = ffmem_tcalloc1(inst_mode)))
			goto end;
		ffkev_init(&imode->kev);
		imode->mode = im;
		if (0 == ffstr_catfmt(&imode->pipename, "\\\\.\\pipe\\%s%Z", IM_PIPE_NAME))
			goto end;

		fffd ph;
		if (FF_BADFD != (ph = fffile_open(imode->pipename.ptr, O_WRONLY))) {
			pipe_add_inputfiles(ph);
			ffpipe_close(ph);
			goto end;
		}
	}

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (imode != NULL) {
		if (0 != pipe_listen(imode->pipename.ptr, imode))
			syserrlog(core, NULL, "gui", "single instance mode: ", "pipe listen");
		ffarr_free(&imode->pipename);
	}

	open_input();

	core->sig(FMED_START);

end:
	if (imode != NULL) {
		if (imode->kev.fd != FF_BADFD)
			ffpipe_close(imode->kev.fd);
		ffarr_free(&imode->pipename);
		ffmem_free(imode);
	}

	core_free();
	return 0;
}
