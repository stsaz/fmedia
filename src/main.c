/** fmedia terminal startup.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <cmd.h>

#include <FF/audio/pcm.h>
#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/sys/dir.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FFOS/sig.h>
#include <FFOS/error.h>
#include <FFOS/process.h>


#define dbglog0(...)  fmed_dbglog(core, NULL, "main", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "main", __VA_ARGS__)
#define syserrlog0(...)  fmed_syserrlog(core, NULL, "main", __VA_ARGS__)


#if !defined _DEBUG
#define FMED_CRASH_HANDLER
#endif

struct gctx {
	fmed_cmd *cmd;
	void *rec_trk;
	const fmed_track *track;
	const fmed_queue *qu;
	uint psexit; //process exit code

	ffdl core_dl;
	fmed_core* (*core_init)(char **argv, char **env);
	void (*core_free)(void);
};
static struct gctx *g;
static fmed_core *core;


static void open_input(void *udata);
static int rec_tracks_start(fmed_cmd *cmd, fmed_trk *trkinfo);
static void rec_lpback_new_track(fmed_cmd *cmd);

// TRACK MONITOR
static void mon_onsig(void *trk, uint sig);
static const struct fmed_trk_mon mon_iface = { &mon_onsig };

//LOG
static void std_log(uint flags, fmed_logdata *ld);
static const fmed_log std_logger = {
	&std_log
};

#include <cmdline.h>

// TIME :TID [LEVEL] MOD: *ID: {"IN_FILENAME": } TEXT
static void std_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + FFCNT(buf) - FFSLEN("\n");

	if (flags != FMED_LOG_USER) {
		if (ld->tid != 0) {
			s += ffs_fmt(s, end, "%s :%xU [%s] %s: "
				, ld->stime, ld->tid, ld->level, ld->module);
		} else {
			s += ffs_fmt(s, end, "%s [%s] %s: "
				, ld->stime, ld->level, ld->module);
		}

		if (ld->ctx != NULL)
			s += ffs_fmt(s, end, "%S:\t", ld->ctx);
	}

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER && ld->trk != NULL) {
		const char *infn = g->track->getvalstr(ld->trk, "input");
		if (infn != FMED_PNULL)
			s += ffs_fmt(s, end, "\"%s\": ", infn);
	}

	s += ffs_fmtv(s, end, ld->fmt, ld->va);

	if (flags & FMED_LOG_SYS)
		s += ffs_fmt(s, end, ": %E", fferr_last());

	*s++ = '\n';

	uint lev = flags & _FMED_LOG_LEVMASK;
	fffd fd = (lev > FMED_LOG_USER && !core->props->stdout_busy) ? ffstdout : ffstderr;
	ffstd_write(fd, buf, s - buf);
}


static void mon_onsig(void *trk, uint sig)
{
	dbglog0("%s: %d", FF_FUNC, sig);
	fmed_trk *ti = g->track->conf(trk);
	switch (sig) {
	case FMED_TRK_ONCLOSE:
		if (trk == g->rec_trk) {
			g->rec_trk = NULL;
			core->sig(FMED_STOP);
		}

		if (ti->type == FMED_TRK_TYPE_PLIST
			&& !g->cmd->gui)
			core->sig(FMED_STOP);

		if (ti->err)
			g->psexit = 1;
		break;

	case FMED_TRK_ONLAST:
		if (g->cmd->gui)
			break;
		if (g->rec_trk != NULL) {
			if (g->cmd->until_plback_end)
				g->track->cmd(g->rec_trk, FMED_TRACK_STOP);
			break;
		}
		core->sig(FMED_STOP);
		break;
	}
}


static const ffuint signals[] = { SIGINT };

static void crash_handler(struct ffsig_info *inf);

static void signal_handler(struct ffsig_info *info)
{
	if (core != NULL)
		dbglog0("received signal:%d", info->sig);
	switch (info->sig) {
	case FFSIG_INT:
		g->track->cmd((void*)-1, FMED_TRACK_STOPALL_EXIT);
		break;
	default:
		crash_handler(info);
	}
}

static void qu_setprops(fmed_cmd *fmed, const fmed_queue *qu, fmed_que_entry *qe);

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
		qu->cmdv(FMED_QUE_SETTRACKPROPS, qe, trkinfo);
		qu_setprops(g->cmd, qu, qe);
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

static void qu_setprops(fmed_cmd *fmed, const fmed_queue *qu, fmed_que_entry *qe)
{
	if (fmed->trackno != NULL) {
		qu->meta_set(qe, FFSTR("input_trackno"), fmed->trackno, ffsz_len(fmed->trackno), FMED_QUE_TRKDICT);
		ffmem_free0(fmed->trackno);
	}

	if (fmed->playdev_name != 0)
		qu_setval(qu, qe, "playdev_name", fmed->playdev_name);

	if (fmed->out_copy) {
		qu_setval(qu, qe, "out-copy", fmed->out_copy);
		if (fmed->stream_copy)
			qu_setval(qu, qe, "out_stream_copy", 1);
		if (fmed->outfn.len != 0)
			qu->meta_set(qe, FFSTR("out_filename"), fmed->outfn.ptr, fmed->outfn.len, FMED_QUE_TRKDICT);

	} else {
		if (fmed->outfn.len != 0 && !fmed->rec)
			qu->meta_set(qe, FFSTR("output"), fmed->outfn.ptr, fmed->outfn.len, FMED_QUE_TRKDICT);
	}

	if (fmed->rec)
		qu_setval(qu, qe, "low_latency", 1);

	if (fmed->meta.len != 0)
		qu->meta_set(qe, FFSTR("meta"), fmed->meta.ptr, fmed->meta.len, FMED_QUE_TRKDICT);
}

static void trk_prep(fmed_cmd *fmed, fmed_trk *trk)
{
	trk->input_info = fmed->info;
	trk->show_tags = fmed->tags;
	trk->include_files = fmed->include_files;
	trk->exclude_files = fmed->exclude_files;
	if (fmed->fseek != 0)
		trk->input.seek = fmed->fseek;
	if (fmed->seek_time != 0)
		trk->audio.seek = fmed->seek_time;
	if (fmed->until_time != 0)
		trk->audio.until = fmed->until_time;
	if (fmed->split_time != 0)
		trk->audio.split = fmed->split_time;
	if (fmed->prebuffer != 0)
		trk->a_prebuffer = fmed->prebuffer;

	trk->out_overwrite = fmed->overwrite;
	trk->out_preserve_date = fmed->preserve_date;

	if (fmed->out_format != 0)
		trk->audio.convfmt.format = fmed->out_format;
	if (fmed->out_channels != 0)
		trk->audio.convfmt.channels = fmed->out_channels;
	if (fmed->out_rate != 0)
		trk->audio.convfmt.sample_rate = fmed->out_rate;

	trk->pcm_peaks = fmed->pcm_peaks;
	trk->pcm_peaks_crc = fmed->pcm_crc;
	trk->use_dynanorm = fmed->dynanorm;
	trk->a_start_level = ffabs(fmed->start_level);
	trk->a_stop_level = ffabs(fmed->stop_level);
	trk->a_stop_level_time = fmed->stop_level_time;
	trk->a_stop_level_mintime = fmed->stop_level_mintime;

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
	trk->audio.auto_attenuate_ceiling = fmed->auto_attenuate;

	if (fmed->aac_qual != (uint)-1)
		trk->aac.quality = fmed->aac_qual;
	if (fmed->aac_profile != NULL)
		ffstr_setz(&trk->aac.profile, fmed->aac_profile);
	if (fmed->vorbis_qual != -255)
		trk->vorbis.quality = (fmed->vorbis_qual + 1.0) * 10;
	if (fmed->opus_brate != 0)
		trk->opus.bitrate = fmed->opus_brate;
	if (fmed->mpeg_qual != 0xffff)
		trk->mpeg.quality = fmed->mpeg_qual;
	if (fmed->flac_complevel != 0xff)
		trk->flac.compression = fmed->flac_complevel;
	if (fmed->cue_gaps != 0xff)
		trk->cue.gaps = fmed->cue_gaps;

	if (fmed->stream_copy && !fmed->out_copy)
		trk->stream_copy = 1;

	trk->print_time = fmed->print_time;
}

static void open_input(void *udata)
{
	char **pfn;
	const fmed_track *track = g->track;
	const fmed_queue *qu = g->qu;
	fmed_que_entry e, *qe;
	void *first = NULL;
	fmed_cmd *fmed = udata;

	fmed_trk trkinfo;
	track->copy_info(&trkinfo, NULL);
	trk_prep(fmed, &trkinfo);

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

		qu->cmdv(FMED_QUE_SETTRACKPROPS, qe, &trkinfo);
		qu_setprops(fmed, qu, qe);
	}
	FFARR_FREE_ALL_PTR(&fmed->in_files, ffmem_free, char*);

	ffstr ext;
	ffpath_split3(fmed->outfn.ptr, fmed->outfn.len, NULL, NULL, &ext);
	if (ffstr_eqz(&ext, "m3u8") || ffstr_eqz(&ext, "m3u")) {
		void *trk;
		if (NULL == (trk = track->create(FMED_TRK_TYPE_PLIST, NULL)))
			goto end;

		fmed_trk *ti = track->conf(trk);
		track->copy_info(ti, &trkinfo);

		track->setvalstr(trk, "output", fmed->outfn.ptr);
		track->cmd(trk, FMED_TRACK_START);
		goto end;
	}

	if (first != NULL) {
		if (fmed->mix)
			qu->cmd(FMED_QUE_MIX, NULL);
		else if (fmed->outfn.len != 0 && fmed->parallel) {
			core->props->parallel = 1;
			qu->cmdv(FMED_QUE_XPLAY, first);
		} else
			qu->cmd(FMED_QUE_PLAY, first);
	}

	if (fmed->rec) {
		if (0 != rec_tracks_start(udata, &trkinfo))
			goto end;
	}

	if (first == NULL && !fmed->rec && !fmed->gui)
		core->sig(FMED_STOP);

	return;

end:
	return;
}

/** Start a track for recording audio. */
static void* rec_track_start(fmed_cmd *cmd, fmed_trk *trkinfo, uint flags)
{
	const fmed_track *track = g->track;
	void *trk;
	if (NULL == (trk = track->create(FMED_TRACK_REC, NULL)))
		return NULL;

	fmed_trk *ti = track->conf(trk);
	ffpcmex fmt = ti->audio.fmt;
	track->copy_info(ti, trkinfo);
	ti->audio.fmt = fmt;

	if (flags & 1)
		track->setval(trk, "capture_device", cmd->captdev_name);
	else if (flags & 2) {
		track->setval(trk, "loopback_device", cmd->lbdev_name);
		rec_lpback_new_track(cmd);
	}

	if (cmd->outfn.len != 0)
		track->setvalstr(trk, "output", cmd->outfn.ptr);

	track->setval(trk, "low_latency", 1);
	ti->a_in_buf_time = cmd->capture_buf_len;

	g->rec_trk = trk;
	return trk;
}

/** Start all recording tracks
* no --dev-... specified: use default capture device
* --dev-capture specified: use capture device
* --dev-loopback specified: use loopback device
* --dev-capture AND --dev-loopback specified: use capture device and loopback device
*/
static int rec_tracks_start(fmed_cmd *cmd, fmed_trk *trkinfo)
{
	void *trk0 = NULL, *trk1 = NULL;

	if (cmd->captdev_name == 0
		&& cmd->lbdev_name == (uint)-1) {
		if (NULL == (trk0 = rec_track_start(cmd, trkinfo, 0)))
			goto end;
	}

	if (cmd->captdev_name != 0) {
		if (NULL == (trk0 = rec_track_start(cmd, trkinfo, 1)))
			goto end;
	}

	if (cmd->lbdev_name != (uint)-1) {
		if (NULL == (trk1 = rec_track_start(cmd, trkinfo, 2)))
			goto end;
		// Note: g->rec_trk holds only one track
	}

	// try to start both tracks at the same time
	if (trk0 != NULL)
		g->track->cmd(trk0, FMED_TRACK_START);
	if (trk1 != NULL)
		g->track->cmd(trk1, FMED_TRACK_START);
	return 0;

end:
	if (trk0 != NULL)
		g->track->cmd(trk0, FMED_TRACK_STOP);
	return -1;
}

/** Create a track to support recording from WASAPI in loopback mode.
It generates silence and plays it via an audio device,
 so data from WASAPI in looopback mode can be read continuously. */
static void rec_lpback_new_track(fmed_cmd *cmd)
{
	const fmed_track *track = g->track;
	void *trk;
	int r = 0;

	if (NULL == (trk = track->create(FMED_TRK_TYPE_NONE, NULL)))
		return;

	fmed_trk *info = track->conf(trk);
	info->audio.fmt.format = FFPCM_16;
	info->audio.fmt.channels = 2;
	info->audio.fmt.sample_rate = 44100;
	if (cmd->lbdev_name != (uint)-1)
		track->setval(trk, "playdev_name", cmd->lbdev_name);

	r |= track->cmd(trk, FMED_TRACK_ADDFILT, "#soundmod.silgen");
	r |= track->cmd(trk, FMED_TRACK_ADDFILT, "wasapi.out");
	if (r != 0) {
		track->cmd(trk, FMED_TRACK_STOP);
		return;
	}
	track->cmd(trk, FMED_TRACK_START);
}

static int gcmd_send(const fmed_globcmd_iface *globcmd)
{
	if (0 != globcmd->write(g->cmd->globcmd.ptr, g->cmd->globcmd.len)) {
		return -1;
	}

	return 0;
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
	if (g->core_init == NULL || g->core_free == NULL) {
		fffile_fmt(ffstderr, NULL, "can't resolve functions from %s: %s\n"
			, a.ptr, ffdl_errstr());
		goto end;
	}

	g->core_dl = dl;
	dl = NULL;
	rc = 0;

end:
	FF_SAFECLOSE(dl, NULL, ffdl_close);
	ffarr_free(&a);
	return rc;
}

#if defined FF_WIN
#define OS_STR  "win"
#elif defined FF_BSD
#define OS_STR  "bsd"
#elif defined FF_APPLE
#define OS_STR  "mac"
#else
#define OS_STR  "linux"
#endif

#if defined FF_WIN
	#ifdef FF_64
	#define CPU_STR  "x64"
	#else
	#define CPU_STR  "x86"
	#endif
#else
	#ifdef FF_64
	#define CPU_STR  "amd64"
	#else
	#define CPU_STR  "i686"
	#endif
#endif

extern void _crash_handler(const char *fullname, const char *version, struct ffsig_info *inf);

/** Called by FFOS on program crash. */
static void crash_handler(struct ffsig_info *inf)
{
#ifdef FMED_CRASH_HANDLER
	const char *ver = (core != NULL) ? core->props->version_str : "";
	_crash_handler("fmedia (" OS_STR "-" CPU_STR ")", ver, inf);
#endif
}

int main(int argc, char **argv, char **env)
{
	int rc = 1, r;
	fmed_cmd *gcmd;

	ffmem_init();
	if (NULL == (g = ffmem_new(struct gctx)))
		return 1;

#ifdef FMED_CRASH_HANDLER
	static const uint sigs_fault[] = { FFSIG_SEGV, FFSIG_ILL, FFSIG_FPE, FFSIG_ABORT };
	ffsig_subscribe(signal_handler, sigs_fault, FF_COUNT(sigs_fault));
	// ffsig_raise(FFSIG_SEGV);
#endif

	if (0 != loadcore(argv[0]))
		goto end;

	if (NULL == (core = g->core_init(argv, env)))
		goto end;
	ffenv_init(NULL, env);
	fffile_fmt(ffstderr, NULL, "fmedia v%s (" OS_STR "-" CPU_STR ")\n"
		, core->props->version_str);
	core->cmd(FMED_SETLOG, &std_logger);
	g->cmd = ffmem_new(fmed_cmd);
	cmd_init(g->cmd);
	gcmd = g->cmd;

	if (argc == 1) {
		arg_usage();
		rc = 0;
		goto end;
	}

	if (0 != (r = fmed_cmdline(argc, argv, 1))) {
		if (r == -1)
			rc = 0;
		goto end;
	}
	core->props->gui = gcmd->gui;
	core->props->tui = !gcmd->notui;

	if (0 != core->cmd(FMED_CONF, gcmd->conf_fn))
		goto end;

	ffmem_safefree(gcmd->conf_fn);

	if (0 != (r = fmed_cmdline(argc, argv, 0))) {
		if (r == -1)
			rc = 0;
		goto end;
	}

	if (gcmd->bground) {
		if (gcmd->bgchild)
			ffterm_detach();
		else {
			ffps ps = ffps_createself_bg("--background-child");
			if (ps == FFPS_INV) {
				syserrlog(core, NULL, "core", "failed to spawn background process");
				goto end;

			} else if (ps != 0) {
				core->log(FMED_LOG_INFO, NULL, "core", "spawned background process: PID %u", ffps_id(ps));
				(void)ffps_close(ps);
				rc = 0;
				goto end;
			}
		}
	}

	if (NULL == (g->track = core->getmod("#core.track")))
		goto end;
	if (NULL == (g->qu = core->getmod("#queue.queue")))
		goto end;
	g->qu->cmdv(FMED_QUE_SET_RANDOM, (uint)g->cmd->list_random);
	g->qu->cmdv(FMED_QUE_SET_REPEATALL, (uint)g->cmd->repeat_all);
	g->qu->cmdv(FMED_QUE_SET_NEXTIFERROR, (uint)g->cmd->notui);
	g->qu->cmdv(FMED_QUE_SET_QUITIFDONE, (uint)!g->cmd->gui);

	const fmed_globcmd_iface *globcmd = NULL;
	ffbool gcmd_listen = 0;
	if (gcmd->globcmd.len != 0
		&& NULL != (globcmd = core->getmod("#globcmd.globcmd"))) {

		if (ffstr_eqcz(&gcmd->globcmd, "listen"))
			gcmd_listen = 1;

		else if (0 == globcmd->ctl(FMED_GLOBCMD_OPEN, g->cmd->globcmd_pipename)) {
			gcmd_send(globcmd);
			rc = 0;
			goto end;
		}
	}

	static const int sigs_block[] = { SIGIO, SIGCHLD };
	ffsig_mask(SIG_BLOCK, sigs_block, FF_COUNT(sigs_block));

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (gcmd_listen) {
		globcmd->ctl(FMED_GLOBCMD_START, g->cmd->globcmd_pipename);
		ffmem_safefree0(g->cmd->globcmd_pipename);
	}

	if (0 != ffsig_subscribe(signal_handler, signals, FF_COUNT(signals))) {
		syserrlog(core, NULL, "core", "%s", "ffsig_subscribe()");
		goto end;
	}

	fftask_set(&gcmd->tsk_start, &open_input, g->cmd);
	core->task(&gcmd->tsk_start, FMED_TASK_POST);

	g->track->fmed_trk_monitor(NULL, &mon_iface);

	core->sig(FMED_START);
	rc = g->psexit;
	dbglog(core, NULL, "core", "exit code: %d", rc);

end:
	if (core != NULL) {
		g->core_free();
	}
	FF_SAFECLOSE(g->core_dl, NULL, ffdl_close);
	cmd_destroy(g->cmd);
	ffmem_free(g->cmd);
	ffmem_free(g);
	return rc;
}
