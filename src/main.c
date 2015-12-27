/** fmedia config.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/audio/pcm.h>
#include <FF/data/psarg.h>
#include <FF/data/utf8.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FFOS/sig.h>
#include <FFOS/error.h>
#include <FFOS/process.h>


static fmedia *fmed;
static fmed_core *core;

FF_IMP fmed_core* core_init(fmedia **ptr, fmed_log_t logfunc);
FF_IMP void core_free(void);

static int fmed_cmdline(int argc, char **argv, uint main_only);
static int fmed_arg_usage(void);
static int fmed_arg_skip(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_pcmfmt(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_listdev(void);
static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val);

static int pcm_formatstr(const char *s, size_t len);
static int open_input(void);
static void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);


static const ffpars_arg fmed_cmdline_args[] = {
	{ "",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_infile) }

	, { "mix",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, mix) }
	, { "repeat-all",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, repeat_all) }
	, { "seek",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }
	, { "fseek",  FFPARS_TINT | FFPARS_F64BIT,  FFPARS_DSTOFF(fmedia, fseek) }
	, { "until",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }
	, { "track",  FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(fmedia, trackno) }
	, { "volume",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, volume) }
	, { "gain",  FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmedia, gain) }

	, { "info",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, info) }

	, { "out",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, outfn) }
	, { "outdir",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, outdir) }

	, { "record",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, rec) }

	, { "list-dev",  FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_listdev) }
	, { "dev",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, playdev_name) }
	, { "dev-capture",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, captdev_name) }

	, { "wav-format",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_pcmfmt) }

	, { "ogg-quality",  FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmedia, ogg_qual) }
	, { "mpeg-quality",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(fmedia, mpeg_qual) }
	, { "cue-gaps",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, cue_gaps) }
	, { "pcm-crc",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, pcm_crc) }
	, { "pcm-peaks",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, pcm_peaks) }
	, { "preserve-date",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, preserve_date) }

	, { "overwrite",  FFPARS_SETVAL('y') | FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, overwrite) }
	, { "silent",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, silent) }
	, { "gui",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, gui) }
	, { "debug",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, debug) }
	, { "help",  FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) }
};

static const ffpars_arg fmed_cmdline_main_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_skip) },
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_skip) },
	{ "gui",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, gui) },
	{ "help",	FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) },
};


static int fmed_arg_usage(void)
{
	char buf[4096];
	ssize_t n;
	char *fn;
	fffd f;

	if (NULL == (fn = core->getpath(FFSTR("help.txt"))))
		return FFPARS_ESYS;

	f = fffile_open(fn, O_RDONLY | O_NOATIME);
	ffmem_free(fn);
	if (f == FF_BADFD)
		return FFPARS_ELAST;
	n = fffile_read(f, buf, sizeof(buf));
	fffile_close(f);
	if (n > 0)
		fffile_write(ffstdout, buf, n);
	return FFPARS_ELAST;
}

static int fmed_arg_pcmfmt(ffparser_schem *p, void *obj, const ffstr *val)
{
	int i;
	if (-1 == (i = pcm_formatstr(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	fmed->wav_formt = i;
	return 0;
}

static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val)
{
	char **fn;
	if (NULL == ffarr_grow(&fmed->in_files, 1, 0))
		return FFPARS_ESYS;
	fn = ffarr_push(&fmed->in_files, char*);
	*fn = val->ptr;
	return 0;
}

static int fmed_arg_listdev(void)
{
	core->sig(FMED_LISTDEV);
	return FFPARS_ELAST;
}

static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val)
{
	uint i;
	ffdtm dt;
	fftime t;
	if (val->len != fftime_fromstr(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFPARS_EBADVAL;

	fftime_join(&t, &dt, FFTIME_TZNODATE);
	i = fftime_ms(&t);

	if (!ffsz_cmp(p->curarg->name, "seek"))
		fmed->seek_time = i;
	else
		fmed->until_time = i;
	return 0;
}

static int fmed_arg_skip(ffparser_schem *p, void *obj, const ffstr *val)
{
	return 0;
}

static int fmed_cmdline(int argc, char **argv, uint main_only)
{
	ffparser_schem ps;
	ffparser p;
	ffpars_ctx ctx = {0};
	int r = 0, i;
	int ret = 1;

	if (main_only)
		ffpars_setargs(&ctx, fmed, fmed_cmdline_main_args, FFCNT(fmed_cmdline_main_args));
	else
		ffpars_setargs(&ctx, fmed, fmed_cmdline_args, FFCNT(fmed_cmdline_args));

	if (0 != ffpsarg_scheminit(&ps, &p, &ctx)) {
		errlog(core, NULL, "core", "cmd line parser", NULL);
		return 1;
	}

	for (i = 1;  i < argc; ) {
		int n = 0;
		r = ffpsarg_parse(&p, argv[i], &n);
		i += n;

		r = ffpars_schemrun(&ps, r);

		if (r == FFPARS_ELAST)
			goto fail;

		if (ffpars_iserr(r))
			break;
	}

	if (!ffpars_iserr(r))
		r = ffpsarg_schemfin(&ps);

	if (ffpars_iserr(r)) {
		errlog(core, NULL, "core", "cmd line parser: %s"
			, (r == FFPARS_ESYS) ? fferr_strp(fferr_last()) : ffpars_errstr(r));
		goto fail;
	}

	ret = 0;

fail:
	ffpars_schemfree(&ps);
	ffpars_free(&p);
	return ret;
}

static int pcm_formatstr(const char *s, size_t len)
{
	if (ffs_eqcz(s, len, "16le"))
		return FFPCM_16LE;
	else if (ffs_eqcz(s, len, "32le"))
		return FFPCM_32LE;
	else if (ffs_eqcz(s, len, "float"))
		return FFPCM_FLOAT;
	return -1;
}


static void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + FFCNT(buf) - FFSLEN("\n");

	s += ffs_fmt(s, end, "%s %s %s: ", stime, level, module);

	if (id != NULL)
		s += ffs_fmt(s, end, "%S:\t", id);

	s += ffs_fmtv(s, end, fmt, va);

	*s++ = '\n';

	ffstd_write(fd, buf, s - buf);
}


#if defined FF_MSVC || defined FF_MINGW
enum {
	SIGINT = 1
};
#endif

static const int sigs[] = { SIGINT };

static void fmed_onsig(void *udata)
{
	const fmed_track *track;
	int sig;
	ffsignal *sg = udata;

	if (sg != NULL && -1 == (sig = ffsig_read(sg)))
		return;

	if (NULL == (track = core->getmod("#core.track")))
		return;
	track->cmd((void*)-1, FMED_TRACK_STOPALL);
}

#ifdef FF_WIN
static BOOL __stdcall fmed_ctrlhandler(DWORD ctrl)
{
	if (ctrl == CTRL_C_EVENT) {
		fmed_onsig(NULL);
		ffkqu_post(fmed->kq, &fmed->evposted, NULL);
		return 1;
	}
	return 0;
}
#endif

static int open_input(void)
{
	char **pfn;
	const fmed_track *track;
	const fmed_queue *qu;
	uint added = 0;
	if (NULL == (qu = core->getmod("#queue.queue")))
		goto end;

	FFARR_WALK(&fmed->in_files, pfn) {
		fmed_que_entry e;
		ffmem_tzero(&e);
		ffstr_setz(&e.url, *pfn);
		qu->add(&e);
		ffmem_free(*pfn);
		added++;
	}
	ffarr_free(&fmed->in_files);

	if (added != 0) {
		if (!fmed->mix)
			qu->cmd(FMED_QUE_PLAY, NULL);
		else
			qu->cmd(FMED_QUE_MIX, NULL);
	}

	if (fmed->rec) {
		void *trk;
		if (NULL == (track = core->getmod("#core.track")))
			goto end;
		if (NULL == (trk = track->create(FMED_TRACK_REC, NULL)))
			goto end;

		if (fmed->outfn.len != 0) {
			track->setvalstr(trk, "output", fmed->outfn.ptr);
		}

		track->cmd(trk, FMED_TRACK_START);
	}

	if (added == 0 && !fmed->rec)
		return 1;

	return 0;

end:
	return 1;
}

int main(int argc, char **argv)
{
	ffsignal sigs_task = {0};

	ffmem_init();
	ffutf8_init();
	ffsig_init(&sigs_task);

	fffile_writecz(ffstdout, "fmedia v" FMED_VER "\n");

	if (NULL == (core = core_init(&fmed, &fmed_log)))
		return 1;

	{
	char fn[FF_MAXPATH];
	ffstr path;
	const char *p = ffps_filename(fn, sizeof(fn), argv[0]);
	if (p == NULL)
		return 1;
	if (NULL == ffpath_split2(p, ffsz_len(p), &path, NULL))
		return 1;
	if (NULL == ffstr_copy(&fmed->root, path.ptr, path.len + FFSLEN("/")))
		return 1;
	}

	if (argc == 1) {
		fmed_arg_usage();
		return 0;
	}

	if (0 != fmed_cmdline(argc, argv, 1))
		goto end;

	if (0 != core->sig(FMED_CONF))
		goto end;

	if (0 != fmed_cmdline(argc, argv, 0))
		goto end;

	if (0 != core->sig(FMED_OPEN))
		goto end;

	ffsig_mask(SIG_BLOCK, sigs, FFCNT(sigs));
	sigs_task.udata = &sigs_task;
	if (0 != ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), &fmed_onsig)) {
		syserrlog(core, NULL, "core", "%s", "ffsig_ctl()");
		goto end;
	}

#ifdef FF_WIN
	SetConsoleCtrlHandler(&fmed_ctrlhandler, TRUE);
#endif

	if (0 != open_input())
		goto end;

#if defined FF_WIN && !defined _DEBUG
	if (fmed->gui && !(core->loglev & FMED_LOG_DEBUG))
		FreeConsole();
#endif

	core->sig(FMED_START);

end:
	ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), NULL);
	core_free();
	return 0;
}
