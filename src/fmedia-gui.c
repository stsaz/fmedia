/** Run fmedia with GUI.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/path.h>
#include <FFOS/process.h>
#include <FFOS/mem.h>


static fmedia *fmed;
static fmed_core *core;


FF_IMP fmed_core* core_init(fmedia **ptr, fmed_log_t logfunc);
FF_IMP void core_free(void);

static void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);


static void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va)
{
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	ffmem_init();

	if (NULL == (core = core_init(&fmed, &fmed_log)))
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

	if (0 != core->sig(FMED_OPEN))
		goto end;

	core->sig(FMED_START);

end:
	core_free();
	return 0;
}
