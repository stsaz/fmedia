/** fmedia terminal startup.
Copyright (c) 2015 Simon Zolin */

#include <core-cmd.h>

#include <FF/audio/pcm.h>
#include <FF/data/psarg.h>
#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/dir.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FFOS/sig.h>
#include <FFOS/error.h>
#include <FFOS/process.h>


static fmed_cmd *gcmd;
#define fmed  gcmd
static fmed_core *core;

FF_IMP fmed_core* core_init(fmed_cmd **ptr, char **argv);
FF_IMP void core_free(void);

static int fmed_cmdline(int argc, char **argv, uint main_only);
static int fmed_arg_usage(void);
static int fmed_arg_skip(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_listdev(void);
static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_install(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_channels(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_arg_format(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_arg_input_chk(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_out_chk(ffparser_schem *p, void *obj, const ffstr *val);

static void open_input(void *udata);
static void fmed_onsig(void *udata);

//LOG
static void std_log(uint flags, fmed_logdata *ld);
static const fmed_log std_logger = {
	&std_log
};


static const ffpars_arg fmed_cmdline_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_infile) },

	//QUEUE
	{ "repeat-all",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, repeat_all) },
	{ "track",	FFPARS_TCHARPTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmed_cmd, trackno) },

	//AUDIO DEVICES
	{ "list-dev",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_listdev) },
	{ "dev",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, playdev_name) },
	{ "dev-capture",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, captdev_name) },
	{ "dev-loopback",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, lbdev_name) },

	//AUDIO FORMAT
	{ "format",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_format) },
	{ "rate",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, out_rate) },
	{ "channels",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_channels) },

	//INPUT
	{ "record",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, rec) },
	{ "mix",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, mix) },
	{ "seek",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) },
	{ "until",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) },
	{ "fseek",	FFPARS_TINT | FFPARS_F64BIT,  FFPARS_DSTOFF(fmed_cmd, fseek) },
	{ "info",	FFPARS_SETVAL('i') | FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, info) },
	{ "tags",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, tags) },
	{ "meta",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmed_cmd, meta) },

	//FILTERS
	{ "volume",	FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmed_cmd, volume) },
	{ "gain",	FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmed_cmd, gain) },
	{ "pcm-peaks",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, pcm_peaks) },
	{ "pcm-crc",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, pcm_crc) },

	//ENCODING
	{ "vorbis.quality",	FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(fmed_cmd, vorbis_qual) },
	{ "opus.bitrate",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, opus_brate) },
	{ "mpeg-quality",	FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(fmed_cmd, mpeg_qual) },
	{ "aac-quality",	FFPARS_TINT,  FFPARS_DSTOFF(fmed_cmd, aac_qual) },
	{ "flac-compression",	FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmed_cmd, flac_complevel) },
	{ "stream-copy",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, stream_copy) },

	//OUTPUT
	{ "out",	FFPARS_SETVAL('o') | FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmed_cmd, outfn) },
	{ "overwrite",	FFPARS_SETVAL('y') | FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, overwrite) },
	{ "out-copy",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, out_copy) },
	{ "preserve-date",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, preserve_date) },

	//OTHER OPTIONS
	{ "globcmd",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  FFPARS_DSTOFF(fmed_cmd, globcmd) },
	{ "conf",	FFPARS_TSTR,  FFPARS_DSTOFF(fmed_cmd, dummy) },
	{ "notui",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, notui) },
	{ "gui",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, gui) },
	{ "print-time",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, print_time) },
	{ "debug",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, debug) },
	{ "help",	FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) },
	{ "cue-gaps",	FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(fmed_cmd, cue_gaps) },

	//INSTALL
	{ "install",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_install) },
	{ "uninstall",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_install) },
};

static const ffpars_arg fmed_cmdline_main_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_input_chk) },
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_skip) },
	{ "out",	FFPARS_SETVAL('o') | FFPARS_TSTR,  FFPARS_DST(&fmed_arg_out_chk) },
	{ "conf",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  FFPARS_DSTOFF(fmed_cmd, conf_fn) },
	{ "notui",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, notui) },
	{ "gui",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, gui) },
	{ "debug",	FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmed_cmd, debug) },
	{ "help",	FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) },
};


static int fmed_arg_usage(void)
{
	ffarr buf = {0};
	ssize_t n;
	char *fn = NULL;
	fffd f = FF_BADFD;
	int r = FFPARS_ESYS;

	if (NULL == (fn = core->getpath(FFSTR("help.txt"))))
		goto done;

	if (FF_BADFD == (f = fffile_open(fn, O_RDONLY | O_NOATIME)))
		goto done;

	if (NULL == ffarr_alloc(&buf, fffile_size(f)))
		goto done;

	n = fffile_read(f, buf.ptr, buf.cap);
	if (n <= 0)
		goto done;

	fffile_write(ffstdout, buf.ptr, n);
	r = FFPARS_ELAST;

done:
	ffmem_safefree(fn);
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffarr_free(&buf);
	return r;
}

static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	char **fn;
	if (NULL == (fn = ffarr_pushgrowT(&cmd->in_files, 4, char*)))
		return FFPARS_ESYS;
	*fn = val->ptr;
	return 0;
}

static int fmed_arg_listdev(void)
{
	fmed_adev_ent *ents = NULL;
	const fmed_modinfo *mod;
	const fmed_adev *adev;
	uint i, ndev;
	ffarr buf = {0};

	ffarr_alloc(&buf, 1024);

	if (NULL == (mod = core->getmod2(FMED_MOD_INFO_ADEV_OUT, NULL, 0))
		|| NULL == (adev = mod->m->iface("adev")))
		goto end;

	ndev = adev->list(&ents, FMED_ADEV_PLAYBACK);
	if ((int)ndev < 0)
		goto end;

	ffstr_catfmt(&buf, "Playback/Loopback:\n");
	for (i = 0;  i != ndev;  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i + 1, ents[i].name);
	}

	if (NULL == (mod = core->getmod2(FMED_MOD_INFO_ADEV_IN, NULL, 0))
		|| NULL == (adev = mod->m->iface("adev")))
		goto end;

	ndev = adev->list(&ents, FMED_ADEV_CAPTURE);
	if ((int)ndev < 0)
		goto end;

	ffstr_catfmt(&buf, "\nCapture:\n");
	for (i = 0;  i != ndev;  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i + 1, ents[i].name);
	}

	fffile_write(ffstdout, buf.ptr, buf.len);

end:
	ffarr_free(&buf);
	if (ents != NULL)
		adev->listfree(ents);
	return FFPARS_ELAST;
}

static int fmed_arg_format(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->out_format = r;
	return 0;
}

static int fmed_arg_channels(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->out_channels = r;
	return 0;
}

static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	uint i;
	ffdtm dt;
	fftime t;
	if (val->len != fftime_fromstr(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFPARS_EBADVAL;

	fftime_join(&t, &dt, FFTIME_TZNODATE);
	i = fftime_ms(&t);

	if (!ffsz_cmp(p->curarg->name, "seek"))
		cmd->seek_time = i;
	else
		cmd->until_time = i;
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

static int fmed_arg_input_chk(ffparser_schem *p, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdin"))
			core->props->stdin_busy = 1;
	}
	return 0;
}

static int fmed_arg_out_chk(ffparser_schem *p, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdout"))
			core->props->stdout_busy = 1;
	}
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


static void std_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + FFCNT(buf) - FFSLEN("\n");

	if (flags != FMED_LOG_USER) {
		s += ffs_fmt(s, end, "%s %s %s: ", ld->stime, ld->level, ld->module);

		if (ld->ctx != NULL)
			s += ffs_fmt(s, end, "%S:\t", ld->ctx);
	}

	s += ffs_fmtv(s, end, ld->fmt, ld->va);

	if (flags & FMED_LOG_SYS)
		s += ffs_fmt(s, end, ": %E", fferr_last());

	*s++ = '\n';

	uint lev = flags & _FMED_LOG_LEVMASK;
	fffd fd = (lev > FMED_LOG_WARN && !core->props->stdout_busy) ? ffstdout : ffstderr;
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

static void qu_setprops(const fmed_queue *qu, fmed_que_entry *qe);

#ifdef FF_WIN
/** Add to queue filenames expanded by wildcard. */
static void* open_input_wcard(const fmed_queue *qu, char *src, const fmed_track *track, const fmed_trk *trkinfo)
{
	ffdirexp de;
	void *first = NULL;

	ffstr s;
	ffstr_setz(&s, src);
	if (ffarr_end(&s) == ffs_findof(s.ptr, s.len, "*?", 2))
		return NULL;

	if (0 != ffdir_expopen(&de, src, 0))
		return NULL;

	const char *fn;
	while (NULL != (fn = ffdir_expread(&de))) {
		fmed_que_entry e, *qe;
		ffmem_tzero(&e);
		ffstr_setz(&e.url, fn);
		qe = qu->add(&e);
		track->copy_info(qe->trk, trkinfo);
		qu_setprops(qu, qe);
		if (first == NULL)
			first = qe;
	}
	ffdir_expclose(&de);
	return first;
}
#endif

static void qu_setval(const fmed_queue *qu, fmed_que_entry *qe, const char *name, int64 val)
{
	qu->meta_set(qe, name, ffsz_len(name), (void*)&val, sizeof(int64), FMED_QUE_TRKDICT | FMED_QUE_NUM);
}

static void qu_setprops(const fmed_queue *qu, fmed_que_entry *qe)
{
	if (fmed->trackno != NULL) {
		qu->meta_set(qe, FFSTR("input_trackno"), fmed->trackno, ffsz_len(fmed->trackno), FMED_QUE_TRKDICT);
		ffmem_free0(fmed->trackno);
	}

	if (fmed->playdev_name != 0)
		qu_setval(qu, qe, "playdev_name", fmed->playdev_name);

	if (fmed->outfn.len != 0 && !fmed->rec)
		qu->meta_set(qe, FFSTR("output"), fmed->outfn.ptr, fmed->outfn.len, FMED_QUE_TRKDICT);

	if (fmed->stream_copy)
		qu_setval(qu, qe, "stream_copy", 1);

	if (fmed->out_copy)
		qu_setval(qu, qe, "out-copy", 1);

	if (fmed->rec)
		qu_setval(qu, qe, "low_latency", 1);

	if (fmed->meta.len != 0)
		qu->meta_set(qe, FFSTR("meta"), fmed->meta.ptr, fmed->meta.len, FMED_QUE_TRKDICT);
}

static void trk_prep(fmed_trk *trk)
{
	trk->input_info = fmed->info;
	if (fmed->fseek != 0)
		trk->input.seek = fmed->fseek;
	if (fmed->seek_time != 0)
		trk->audio.seek = fmed->seek_time;
	if (fmed->until_time != 0)
		trk->audio.until = fmed->until_time;

	trk->out_overwrite = fmed->overwrite;
	trk->out_preserve_date = fmed->preserve_date;

	if (fmed->out_format != 0)
		trk->audio.convfmt.format = fmed->out_format;
	if (fmed->out_channels != 0)
		trk->audio.convfmt.channels = fmed->out_channels;
	if (fmed->out_rate != 0)
		trk->audio.convfmt.sample_rate = fmed->out_rate;

	trk->pcm_peaks_crc = fmed->pcm_crc;

	if (fmed->volume != 100) {
		double db;
		if (fmed->volume < 100)
			db = ffpcm_vol2db(fmed->volume, 48);
		else
			db = ffpcm_vol2db_inc(fmed->volume - 100, 25, 6);
		trk->audio.gain = db * 100;
	}

	if (fmed->gain != 0)
		trk->audio.gain = fmed->gain * 100;

	if (fmed->aac_qual != (uint)-1)
		trk->aac.quality = fmed->aac_qual;
	if (fmed->vorbis_qual != -255)
		trk->vorbis.quality = (fmed->vorbis_qual + 1.0) * 10;
	if (fmed->opus_brate != 0)
		trk->opus.bitrate = fmed->opus_brate;
	if (fmed->mpeg_qual != 0xffff)
		trk->mpeg.quality = fmed->mpeg_qual;
	if (fmed->flac_complevel != 0xff)
		trk->flac.compression = fmed->flac_complevel;
}

static void open_input(void *udata)
{
	char **pfn;
	const fmed_track *track;
	const fmed_queue *qu;
	fmed_que_entry e, *qe;
	void *first = NULL;
	if (NULL == (qu = core->getmod("#queue.queue")))
		goto end;
	if (NULL == (track = core->getmod("#core.track")))
		goto end;

	fmed_trk trkinfo;
	track->copy_info(&trkinfo, NULL);
	trk_prep(&trkinfo);

	FFARR_WALKT(&fmed->in_files, pfn, char*) {

#ifdef FF_WIN
		if (NULL != (qe = open_input_wcard(qu, *pfn, track, &trkinfo))) {
			if (first == NULL)
				first = qe;
			continue;
		}
#endif

		ffmem_tzero(&e);
		ffstr_setz(&e.url, *pfn);
		qe = qu->add(&e);
		if (first == NULL)
			first = qe;

		track->copy_info(qe->trk, &trkinfo);
		qu_setprops(qu, qe);
	}
	FFARR_FREE_ALL_PTR(&gcmd->in_files, ffmem_free, char*);

	if (first != NULL) {
		if (!fmed->mix)
			qu->cmd(FMED_QUE_PLAY, first);
		else
			qu->cmd(FMED_QUE_MIX, NULL);
	}

	if (fmed->rec) {
		void *trk;
		if (NULL == (trk = track->create(FMED_TRACK_REC, NULL)))
			goto end;
		fmed_trk *ti = track->conf(trk);
		ffpcmex fmt = ti->audio.fmt;
		track->copy_info(ti, &trkinfo);
		ti->audio.fmt = fmt;

		if (fmed->lbdev_name != (uint)-1)
			track->setval(trk, "loopback_device", fmed->lbdev_name);
		else if (fmed->captdev_name != 0)
			track->setval(trk, "capture_device", fmed->captdev_name);

		if (fmed->outfn.len != 0)
			track->setvalstr(trk, "output", fmed->outfn.ptr);

		if (fmed->rec)
			track->setval(trk, "low_latency", 1);

		track->cmd(trk, FMED_TRACK_START);
	}

	if (first == NULL && !fmed->rec && !fmed->gui)
		core->sig(FMED_STOP);

	return;

end:
	return;
}

static int gcmd_send(const fmed_globcmd_iface *globcmd)
{
	int r = -1;

	if (0 != globcmd->write(fmed->globcmd.ptr, fmed->globcmd.len)) {
		goto end;
	}

	r = 0;
end:
	ffstr_free(&fmed->globcmd);
	return r;
}

int main(int argc, char **argv)
{
	ffsignal sigs_task = {0};

	ffmem_init();
	ffsig_init(&sigs_task);

	fffile_writecz(ffstderr, "fmedia v" FMED_VER "\n");

	ffsig_mask(SIG_BLOCK, sigs_block, FFCNT(sigs_block));

	if (NULL == (core = core_init(&fmed, argv)))
		return 1;
	fmed->log = &std_logger;

	if (argc == 1) {
		fmed_arg_usage();
		return 0;
	}

	if (0 != fmed_cmdline(argc, argv, 1))
		goto end;

	if (fmed->debug)
		core->loglev = FMED_LOG_DEBUG;

	if (0 != core->cmd(FMED_CONF, gcmd->conf_fn))
		goto end;

	ffmem_safefree(fmed->conf_fn);

	if (0 != fmed_cmdline(argc, argv, 0))
		goto end;

	const fmed_globcmd_iface *globcmd = NULL;
	ffbool gcmd_listen = 0;
	if (fmed->globcmd.len != 0
		&& NULL != (globcmd = core->getmod("#globcmd.globcmd"))) {

		if (ffstr_eqcz(&fmed->globcmd, "listen"))
			gcmd_listen = 1;

		else if (0 == globcmd->ctl(FMED_GLOBCMD_OPEN)) {
			gcmd_send(globcmd);
			goto end;
		}
	}

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (gcmd_listen) {
		globcmd->ctl(FMED_GLOBCMD_START);
	}

	sigs_task.udata = &sigs_task;
	if (0 != ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), &fmed_onsig)) {
		syserrlog(core, NULL, "core", "%s", "ffsig_ctl()");
		goto end;
	}

	fftask_set(&fmed->tsk_start, &open_input, NULL);
	core->task(&fmed->tsk_start, FMED_TASK_POST);

	core->sig(FMED_START);

end:
	ffsig_ctl(&sigs_task, core->kq, sigs, FFCNT(sigs), NULL);
	core_free();
	return 0;
}
