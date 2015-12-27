/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/path.h>
#include <FFOS/process.h>
#include <FFOS/mem.h>


static fmedia *fmed;
static fmed_core *core;
static const fmed_log *lg;


FF_IMP fmed_core* core_init(fmedia **ptr, fmed_log_t logfunc);
FF_IMP void core_free(void);

static void open_input(void);
static void addlog(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);


static void addlog(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va)
{
	if (lg == NULL)
		return;
	lg->log(stime, module, level, id, fmt, va);
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

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	ffmem_init();

	if (NULL == (core = core_init(&fmed, &addlog)))
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

	fmed->gui = 1;
	if (0 != core->sig(FMED_CONF))
		goto end;

	if (NULL == (lg = core->getmod("gui.log")))
		goto end;

	if (0 != core->sig(FMED_OPEN))
		goto end;

	open_input();

	core->sig(FMED_START);

end:
	core_free();
	return 0;
}
