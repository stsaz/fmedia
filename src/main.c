/** fmedia config.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/audio/pcm.h>
#include <FF/data/psarg.h>
#include <FF/data/utf8.h>
#include <FF/dir.h>
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
static int fmed_arg_install(ffparser_schem *p, void *obj, const ffstr *val);

static int pcm_formatstr(const char *s, size_t len);
static int open_input(void);
static void addlog(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);
static void fmed_onsig(void *udata);


static const ffpars_arg fmed_cmdline_args[] = {
	{ "",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_infile) }

	, { "mix",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, mix) }
	, { "repeat-all",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, repeat_all) }
	, { "seek",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }
	, { "fseek",  FFPARS_TINT | FFPARS_F64BIT,  FFPARS_DSTOFF(fmedia, fseek) }
	, { "until",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }
	, { "track",  FFPARS_TCHARPTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, trackno) }
	, { "volume",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, volume) }
	, { "gain",  FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmedia, gain) }

	, { "info",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, info) }
	, { "tags",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, tags) }

	, { "out",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, outfn) }
	, { "outdir",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, outdir) }

	, { "record",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, rec) }

	, { "list-dev",  FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_listdev) }
	, { "dev",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, playdev_name) }
	, { "dev-capture",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, captdev_name) }

	, { "mono",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, out_channels) }
	, { "rate",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, out_rate) }
	, { "wav-format",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_pcmfmt) }

	, { "ogg-quality",  FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmedia, ogg_qual) }
	, { "mpeg-quality",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(fmedia, mpeg_qual) }
	, { "flac-compression",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, flac_complevel) }
	, { "cue-gaps",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmedia, cue_gaps) }
	, { "pcm-crc",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, pcm_crc) }
	, { "pcm-peaks",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, pcm_peaks) }
	, { "preserve-date",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, preserve_date) }
	, { "meta",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, meta) }

	, { "overwrite",  FFPARS_SETVAL('y') | FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, overwrite) }
	, { "silent",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, silent) }
	, { "gui",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, gui) }
	, { "debug",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, debug) }
	, { "help",  FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) }
	,
	{ "install",  FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_install) },
	{ "uninstall",  FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_install) },
};

static const ffpars_arg fmed_cmdline_main_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_skip) },
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_skip) },
	{ "silent",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, silent) },
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

static int fmed_arg_install(ffparser_schem *p, void *obj, const ffstr *val)
{
#ifdef FF_WIN
	const fmed_modinfo *mi = core->insmod("gui.gui", NULL);
	if (mi != NULL)
		mi->m->sig(!ffsz_cmp(p->curarg->name, "install") ? FMED_SIG_INSTALL : FMED_SIG_UNINSTALL);
#endif
	return FFPARS_ELAST;
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
	int r = 0;
	int ret = 1;
	const char *arg;
	ffpsarg a;

	ffpsarg_init(&a, (void*)argv, argc);

	if (main_only)
		ffpars_setargs(&ctx, fmed, fmed_cmdline_main_args, FFCNT(fmed_cmdline_main_args));
	else
		ffpars_setargs(&ctx, fmed, fmed_cmdline_args, FFCNT(fmed_cmdline_args));

	if (0 != ffpsarg_scheminit(&ps, &p, &ctx)) {
		errlog(core, NULL, "core", "cmd line parser", NULL);
		return 1;
	}

	ffpsarg_next(&a); //skip argv[0]

	arg = ffpsarg_next(&a);
	while (arg != NULL) {
		int n = 0;
		r = ffpsarg_parse(&p, arg, &n);
		if (n != 0)
			arg = ffpsarg_next(&a);

		r = ffpsarg_schemrun(&ps);

		if (r == FFPARS_ELAST)
			goto fail;

		if (ffpars_iserr(r))
			break;
	}

	if (!ffpars_iserr(r))
		r = ffpsarg_schemfin(&ps);

	if (ffpars_iserr(r)) {
		errlog(core, NULL, "core", "cmd line parser: near \"%S\": %s"
			, &p.val, (r == FFPARS_ESYS) ? fferr_strp(fferr_last()) : ffpars_errstr(r));
		goto fail;
	}

	ret = 0;

fail:
	ffpsarg_destroy(&a);
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


static void addlog(fffd fd, const char *stime, const char *module, const char *level
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


static const int sigs[] = { SIGINT };
static const int sigs_block[] = { SIGINT, SIGIO };

static void fmed_onsig(void *udata)
{
	const fmed_track *track;
	int sig;
	ffsignal *sg = udata;

	if (-1 == (sig = ffsig_read(sg, NULL)))
		return;

	if (NULL == (track = core->getmod("#core.track")))
		return;
	track->cmd((void*)-1, FMED_TRACK_STOPALL_EXIT);
}

#ifdef FF_WIN
/** Add to queue filenames expanded by wildcard. */
static int open_input_wcard(const fmed_queue *qu, char *src)
{
	ffdirexp de;

	ffstr s;
	ffstr_setz(&s, src);
	if (ffarr_end(&s) == ffs_findof(s.ptr, s.len, "*?", 2))
		return 1;

	if (0 != ffdir_expopen(&de, src, 0))
		return 1;

	const char *fn;
	while (NULL != (fn = ffdir_expread(&de))) {
		fmed_que_entry e;
		ffmem_tzero(&e);
		ffstr_setz(&e.url, fn);
		qu->add(&e);
	}
	ffdir_expclose(&de);
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

#ifdef FF_WIN
		if (0 == open_input_wcard(qu, *pfn)) {
			added = 1;
			ffmem_free(*pfn);
			continue;
		}
#endif

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

	if (added == 0 && !fmed->rec && !fmed->gui)
		return 1;

	return 0;

end:
	return 1;
}

int main(int argc, char **argv)
{
	ffsignal sigs_task = {0};

	ffmem_init();
	ffsig_init(&sigs_task);

	fffile_writecz(ffstdout, "fmedia v" FMED_VER "\n");

	ffsig_mask(SIG_BLOCK, sigs_block, FFCNT(sigs_block));

	if (NULL == (core = core_init(&fmed, &addlog)))
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

	sigs_task.udata = &sigs_task;
	if (0 != ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), &fmed_onsig)) {
		syserrlog(core, NULL, "core", "%s", "ffsig_ctl()");
		goto end;
	}

	if (0 != open_input())
		goto end;

	core->sig(FMED_START);

end:
	ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), NULL);
	core_free();
	return 0;
}
