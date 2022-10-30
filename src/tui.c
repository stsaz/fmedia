/** Terminal UI.
Copyright (c) 2015 Simon Zolin */

#ifdef _WIN32
#include <util/gui-winapi/winapi-shell.h>
#endif
#include <fmedia.h>
#include <util/gui-gtk/unix-shell.h>
#include <util/array.h>
#include <FFOS/error.h>

#define tui_errlog(t, ...)  fmed_errlog(core, (t)->trk, "tui", __VA_ARGS__)

struct tui;

typedef struct gtui {
	const fmed_queue *qu;
	const fmed_track *track;
	ffkevent kev;

	fflock lktrk;
	struct tui *curtrk; //currently playing track
	struct tui *curtrk_rec; //currently recording track

	uint vol;
	uint progress_dots;

	uint mute :1;
} gtui;

static gtui *gt;

typedef struct tui {
	fmed_filt *d;
	void *trk;
	fmed_que_entry *qent;
	uint64 total_samples;
	uint64 played_samples;
	int64 seek_msec;
	uint lastpos;
	uint sample_rate;
	uint sampsize;
	uint total_time_sec;
	ffstr3 buf;
	double maxdb;
	uint nback;

	uint rec :1
		, paused :1
		;
} tui;

static struct tui_conf_t {
	byte echo_off;
	byte file_delete_method;
} tui_conf;

enum {
	SEEK_STEP = 5 * 1000,
	SEEK_STEP_MED = 15 * 1000,
	SEEK_STEP_LARGE = 60 * 1000,
	REC_STATUS_UPDATE = 200, //update recording status timeout (msec)

	VOL_STEP = 5,
	VOL_MAX = 125,
	VOL_LO = /*-*/48,
	VOL_HI = 6,

	MINDB = 40,
};

enum CMDS {
	CMD_PLAY,
	CMD_STOP,
	CMD_NEXT,
	CMD_PREV,
	CMD_SEEKRIGHT,
	CMD_SEEKLEFT,
	CMD_VOLUP,
	CMD_VOLDOWN,
	CMD_MUTE,

	CMD_SHOWTAGS,
	CMD_SAVETRK,
	CMD_LIST_RANDOM,

	CMD_QUIT,

	CMD_MASK = 0xff,

	_CMD_CURTRK_REC = 1 << 26,
	_CMD_F3 = 1 << 27, //use 'cmdfunc3'
	_CMD_CURTRK = 1 << 28, // use 'cmdfunc'.  Call handler only if there's an active track
	_CMD_CORE = 1 << 29,
	_CMD_F1 = 1 << 31, //use 'cmdfunc1'. Can't be used with _CMD_CURTRK.
};

typedef void (*cmdfunc)(tui *t, uint cmd);
typedef void (*cmdfunc3)(tui *t, uint cmd, void *udata);
typedef void (*cmdfunc1)(uint cmd);

static const fmed_core *core;

//FMEDIA MODULE
static const void* tui_iface(const char *name);
static int tui_mod_conf(const char *name, fmed_conf_ctx *conf);
static int tui_sig(uint signo);
static void tui_destroy(void);
static const fmed_mod fmed_tui_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&tui_iface, &tui_sig, &tui_destroy, &tui_mod_conf
};

static int tui_config(fmed_conf_ctx *conf);
static void tui_cmdread(void *param);
static void tui_help(uint cmd);
static int tui_setvol(tui *t, uint vol);

struct key;
static void tui_corecmd(void *param);
static void tui_corecmd_add(const struct key *k, void *udata);


static int conf_file_delete_method(fmed_conf *c, void *obj, ffstr *val)
{
	if (ffstr_eqz(val, "default"))
	{}
	else if (ffstr_eqz(val, "rename"))
		tui_conf.file_delete_method = 1;
	else
		return FMC_EBADVAL;
	return 0;
}
static const fmed_conf_arg tui_conf_args[] = {
	{ "echo_off",	FMC_BOOL8,  FMC_O(struct tui_conf_t, echo_off) },
	{ "file_delete_method",	FMC_STR,  FMC_F(conf_file_delete_method) },
	{}
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_tui_mod;
}

static const fmed_filter fmed_tui;
static const void* tui_iface(const char *name)
{
	if (!ffsz_cmp(name, "tui")) {
		return &fmed_tui;
	}
	return NULL;
}

static int tui_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "tui"))
		return tui_config(ctx);
	return -1;
}

static int tui_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		gt = ffmem_tcalloc1(gtui);
		fflk_init(&gt->lktrk);
		gt->vol = 100;
		uint term_wnd_size = 80;
		if (NULL == (gt->qu = core->getmod("#queue.queue")))
			return 1;
		if (NULL == (gt->track = core->getmod("#core.track")))
			return 1;

		if (core->props->stdin_busy)
			return 0;

		uint attr = FFSTD_LINEINPUT;
		if (tui_conf.echo_off)
			attr |= FFSTD_ECHO;
		ffstd_attr(ffstdin, attr, 0);

#ifdef FF_WIN
		CONSOLE_SCREEN_BUFFER_INFO info = {};
		if (GetConsoleScreenBufferInfo(ffstdout, &info))
			term_wnd_size = info.dwSize.X;

		if (0 != core->cmd(FMED_WOH_INIT)) {
			fmed_warnlog(core, NULL, "tui", "can't start stdin reader");
			return 0;
		}
		fftask t;
		t.handler = &tui_cmdread;
		t.param = gt;
		if (0 != core->cmd(FMED_WOH_ADD, ffstdin, &t)) {
			fmed_warnlog(core, NULL, "tui", "can't start stdin reader");
			return 0;
		}
#else
		ffkev_init(&gt->kev);
		gt->kev.oneshot = 0;
		gt->kev.fd = ffstdin;
		gt->kev.handler = tui_cmdread;
		gt->kev.udata = gt;
		if (0 != ffkev_attach(&gt->kev, core->kq, FFKQU_READ)) {
			fmed_syswarnlog(core, NULL, "tui", "ffkev_attach()");
			return 0;
		}
#endif

		gt->progress_dots = ffmax((int)term_wnd_size - (int)FFSLEN("[] 00:00 / 00:00"), 0);
		break;
	}
	return 0;
}

static void tui_destroy(void)
{
	if (gt == NULL)
		return;

#ifdef FF_WIN
	core->cmd(FMED_WOH_DEL, ffstdin);
#endif

	uint attr = FFSTD_LINEINPUT;
	if (tui_conf.echo_off)
		attr |= FFSTD_ECHO;
	ffstd_attr(ffstdin, attr, attr);

	ffmem_safefree(gt);
}


static int tui_config(fmed_conf_ctx *conf)
{
	tui_conf.echo_off = 1;
	fmed_conf_addctx(conf, &tui_conf, tui_conf_args);
	return 0;
}

static void* tui_open(fmed_filt *d)
{
	tui *t = ffmem_tcalloc1(tui);
	if (t == NULL)
		return NULL;
	t->seek_msec = -1;
	t->lastpos = (uint)-1;
	t->d = d;

	t->qent = (void*)fmed_getval("queue_item");
	t->trk = d->trk;

	if (d->type == FMED_TRK_TYPE_REC) {
		t->rec = 1;
		gt->curtrk_rec = t;
		t->maxdb = -MINDB;

		ffpcm fmt;
		ffpcm_fmtcopy(&fmt, &d->audio.fmt);
		t->sample_rate = fmt.sample_rate;
		t->sampsize = ffpcm_size1(&fmt);

		core->log(FMED_LOG_USER, d->trk, NULL, "Recording...  Source: %s %uHz %s.  %sPress \"s\" to stop."
			, ffpcm_fmtstr(fmt.format), fmt.sample_rate, ffpcm_channelstr(fmt.channels)
			, (d->a_prebuffer != 0) ? "Press \"T\" to start writing to a file.  " : "");

	} else if (d->type == FMED_TRK_TYPE_PLAYBACK) {
		if (t->qent != FMED_PNULL) {
			fflk_lock(&gt->lktrk);
			gt->curtrk = t;
			fflk_unlock(&gt->lktrk);
		}

		uint vol = (gt->mute) ? 0 : gt->vol;
		if (vol != 100)
			tui_setvol(t, vol);
	}

	d->meta_changed = 1;
	return t;
}

static void tui_close(void *ctx)
{
	tui *t = ctx;

	if (t == gt->curtrk) {
		fflk_lock(&gt->lktrk);
		gt->curtrk = NULL;
		fflk_unlock(&gt->lktrk);
	}
	if (t == gt->curtrk_rec)
		gt->curtrk_rec = NULL;
	ffarr_free(&t->buf);
	ffmem_free(t);
}

static void tui_addtags(tui *t, fmed_que_entry *qent, ffarr *buf)
{
	fmed_trk_meta meta;
	ffmem_zero_obj(&meta);
	while (0 == t->d->track->cmd2(t->d->trk, FMED_TRACK_META_ENUM, &meta)) {
		ffsize nt = (meta.name.len < 8) ? 2 : 1;
		ffstr val = meta.val;
		const char *end = ffs_skip_mask(val.ptr, val.len, ffcharmask_printable);
		if (end != ffstr_end(&val))
			ffstr_setz(&val, "<binary data>");
		ffstr_catfmt(buf, "%S%*c%S\n", &meta.name, nt, '\t', &val);
	}
}

static void tui_info(tui *t, fmed_filt *d)
{
	uint64 total_time, tsize;
	uint tmsec;
	const char *input;
	ffstr *tstr, artist = {0}, title = {0};
	ffpcm fmt;

	ffpcm_fmtcopy(&fmt, &d->audio.fmt);
	t->sample_rate = fmt.sample_rate;
	t->sampsize = ffpcm_size1(&fmt);

	if (FMED_PNULL == (input = d->track->getvalstr(d->trk, "input")))
		return;

	total_time = ((int64)t->total_samples != FMED_NULL) ? ffpcm_time(t->total_samples, t->sample_rate) : 0;
	tmsec = (uint)(total_time / 1000);
	t->total_time_sec = tmsec;

	tsize = ((int64)d->input.size != FMED_NULL) ? d->input.size : 0;

	if (FMED_PNULL != (tstr = (void*)d->track->getvalstr3(d->trk, "artist", FMED_TRK_META | FMED_TRK_VALSTR)))
		artist = *tstr;

	if (FMED_PNULL != (tstr = (void*)d->track->getvalstr3(d->trk, "title", FMED_TRK_META | FMED_TRK_VALSTR)))
		title = *tstr;

	fmed_que_entry *qtrk = (void*)d->track->getval(d->trk, "queue_item");
	size_t trkid = (qtrk != FMED_PNULL) ? gt->qu->cmdv(FMED_QUE_ID, qtrk) + 1 : 1;

	t->buf.len = 0;
	ffstr_catfmt(&t->buf, "\n#%L \"%S - %S\" \"%s\" %.02FMB %u:%02u.%03u (%,U samples) %ukbps %s %s %uHz %s"
		, trkid
		, &artist, &title
		, input
		, (double)tsize / (1024 * 1024)
		, tmsec / 60, tmsec % 60, (uint)(total_time % 1000)
		, t->total_samples
		, (d->audio.bitrate + 500) / 1000
		, d->audio.decoder
		, ffpcm_fmtstr(fmt.format)
		, fmt.sample_rate
		, ffpcm_channelstr(fmt.channels));

	if (d->video.width != 0) {
		ffstr_catfmt(&t->buf, "  Video: %s, %ux%u"
			, d->video.decoder, (int)d->video.width, (int)d->video.height);
	}

	ffstr_catfmt(&t->buf, "\n\n");

	if (d->show_tags) {
		tui_addtags(t, t->qent, &t->buf);
	}

	ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
	t->buf.len = 0;
}

static void tui_seek(tui *t, uint cmd, void *udata)
{
	int64 pos = (uint64)t->lastpos * 1000;
	uint by;
	switch ((size_t)udata & FFKEY_MODMASK) {
	case 0:
		by = SEEK_STEP;
		break;
	case FFKEY_ALT:
		by = SEEK_STEP_MED;
		break;
	case FFKEY_CTRL:
		by = SEEK_STEP_LARGE;
		break;
	default:
		return;
	}
	if (cmd == CMD_SEEKRIGHT)
		pos += by;
	else
		pos = ffmax(pos - by, 0);

	fmed_dbglog(core, t->d->trk, "tui", "seek: %U", pos);
	t->seek_msec = pos;
	t->d->seek_req = 1;
	if (t->d->adev_ctx != NULL)
		t->d->adev->cmd(FMED_ADEV_CMD_CLEAR, t->d->adev_ctx);
}

static void tui_vol(tui *t, uint cmd)
{
	uint vol = 0;

	switch (cmd & ~FFKEY_MODMASK) {
	case CMD_VOLUP:
		vol = gt->vol = ffmin(gt->vol + VOL_STEP, VOL_MAX);
		gt->mute = 0;
		break;

	case CMD_VOLDOWN:
		vol = gt->vol = ffmax((int)gt->vol - VOL_STEP, 0);
		gt->mute = 0;
		break;

	case CMD_MUTE:
		gt->mute = !gt->mute;
		vol = (gt->mute) ? 0 : gt->vol;
		break;
	}

	int db = tui_setvol(t, vol);
	core->log(FMED_LOG_USER, t->d->trk, NULL, "Volume: %.02FdB", (double)db / 100);
}

static int tui_setvol(tui *t, uint vol)
{
	int db;
	if (vol <= 100)
		db = ffpcm_vol2db(vol, VOL_LO) * 100;
	else
		db = ffpcm_vol2db_inc(vol - 100, VOL_MAX - 100, VOL_HI) * 100;
	t->d->audio.gain = db;
	return db;
}

static void tui_rmfile(tui *t, uint cmd)
{
	fmed_que_entry *qtrk = t->qent;
	gt->qu->cmd(FMED_QUE_RM, qtrk);
	fmed_infolog(core, NULL, "tui", "Track removed");
}

void file_del(tui *t)
{
	ffvec fn = {};
	fmed_que_entry *qtrk = t->qent;
	const char *url = qtrk->url.ptr;
	if (tui_conf.file_delete_method == 0) {
#ifdef FF_LINUX
		const char *e;
		if (0 != ffui_glib_trash(url, &e)) {
			tui_errlog(t, "can't move file to trash: %s: %s", url, e);
			goto end;
		}
#elif defined FF_WIN
		if (0 != ffui_fop_del(&url, 1, FOF_ALLOWUNDO)) {
			fmed_syserrlog(core, t->trk, "tui", "can't move file to trash: %s", url);
			goto end;
		}
#endif

	} else if (tui_conf.file_delete_method == 1) {
		ffstr_catfmt(&fn, "%S.deleted%Z", &qtrk->url);
		if (0 != fffile_rename(url, fn.ptr)) {
			fmed_syserrlog(core, t->trk, "tui", "can't rename file: %S", &qtrk->url);
			goto end;
		}
	}
	fmed_infolog(core, NULL, "tui", "File deleted");

	gt->qu->cmd(FMED_QUE_RM, qtrk);
end:
	ffvec_free(&fn);
}

static int tui_process(void *ctx, fmed_filt *d)
{
	tui *t = ctx;
	int64 playpos;
	uint playtime;

	if (t->rec) {
		double db = d->audio.maxpeak;
		if (t->maxdb < db)
			t->maxdb = db;

		playtime = ffpcm_time(d->audio.pos, t->sample_rate);
		if (playtime / REC_STATUS_UPDATE == t->lastpos / REC_STATUS_UPDATE)
			goto done;
		t->lastpos = playtime;
		playtime /= 1000;

		if (db < -MINDB)
			db = -MINDB;

		size_t pos = ((MINDB + db) / MINDB) * 10;
		t->buf.len = 0;
		ffstr_catfmt(&t->buf, "%*c%u:%02u  [%*c%*c] %3.02FdB / %.02FdB  "
			, (size_t)t->nback, '\r'
			, playtime / 60, playtime % 60
			, pos, '='
			, (size_t)(10 - pos), '.'
			, db, t->maxdb);

		goto print;
	}

	/*
	v fwd meta !seek:	rdata
	v fwd meta seek:	rdata
	v fwd data !seek:	seek='',rdata
	v back meta !seek:	rmore
	v back data !seek:	rmore
	v back meta seek:	seek=N,rmore
	v fwd data seek:	seek=N,rmore
	v back data seek:	seek=N,rmore
	*/

	if (d->meta_block && (d->flags & FMED_FFWD)) {
		goto pass;
	}

	if (d->meta_changed) {
		d->meta_changed = 0;

		if (d->audio.fmt.format == 0) {
			errlog(core, d->trk, NULL, "audio format isn't set");
			return FMED_RERR;
		}

		t->total_samples = d->audio.total;
		t->played_samples = 0;
		tui_info(t, d);
		if (d->input_info)
			return FMED_RFIN;
	}

	if (t->seek_msec != -1) {
		d->audio.seek = t->seek_msec;
		t->seek_msec = -1;
		return FMED_RMORE; // new seek request
	} else if (!(d->flags & FMED_FFWD)) {
		return FMED_RMORE; // going back without seeking
	} else if (d->data_in.len == 0 && !(d->flags & FMED_FLAST)) {
		return FMED_RMORE; // waiting for audio data
	} else if ((int64)d->audio.seek != FMED_NULL && !d->seek_req) {
		fmed_dbglog(core, d->trk, NULL, "seek: done");
		d->audio.seek = FMED_NULL; // prev. seek is complete
	}

	if (core->props->parallel) {
		/* We don't print progress bars because it would be a mess. */
		d->out = d->data;
		d->outlen = d->datalen;
		return FMED_RDONE;
	}

	if (gt->curtrk_rec != NULL && !t->rec)
		goto done; //don't show playback bar while recording in another track

	if (FMED_NULL == (playpos = d->audio.pos))
		playpos = t->played_samples;
	playtime = (uint)(ffpcm_time(playpos, t->sample_rate) / 1000);
	if (playtime == t->lastpos) {
		goto done;
	}
	t->lastpos = playtime;

	if ((int64)t->total_samples == FMED_NULL
		|| (uint64)playpos >= t->total_samples) {

		t->buf.len = 0;
		ffstr_catfmt(&t->buf, "%*c%u:%02u"
			, (size_t)t->nback, '\r'
			, playtime / 60, playtime % 60);

		goto print;
	}

	t->buf.len = 0;
	uint dots = gt->progress_dots;
	ffstr_catfmt(&t->buf, "%*c[%*c%*c] %u:%02u / %u:%02u"
		, (size_t)t->nback, '\r'
		, (size_t)(playpos * dots / t->total_samples), '='
		, (size_t)(dots - (playpos * dots / t->total_samples)), '.'
		, playtime / 60, playtime % 60
		, t->total_time_sec / 60, t->total_time_sec % 60);

print:
	fffile_write(ffstderr, t->buf.ptr, t->buf.len);
	t->nback = 1;
	if (core->loglev == FMED_LOG_DEBUG)
		t->nback = 0;
	t->buf.len = 0;

done:
	t->played_samples += d->datalen / t->sampsize;
	dbglog(core, d->trk, NULL, "samples: +%L [%U] at %U"
		, d->datalen / t->sampsize, t->played_samples, d->audio.pos);

pass:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST) {
		fffile_write(ffstderr, "\n", 1);
		return FMED_RDONE;
	}
	return (d->type == FMED_TRK_TYPE_PLAYBACK) ? FMED_RDATA : FMED_ROK;
}

static const fmed_filter fmed_tui = { tui_open, tui_process, tui_close };

static void tui_op_trk(struct tui *t, uint cmd)
{
	switch (cmd) {
	case CMD_SHOWTAGS:
		t->buf.len = 0;
		tui_addtags(t, t->qent, &t->buf);
		ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
		t->buf.len = 0;
		break;

	case CMD_SAVETRK:
		fmed_infolog(core, t->trk, "tui", "Saving track to disk");
		t->d->save_trk = 1;
		break;
	}
}

static void tui_op(uint cmd)
{
	switch (cmd) {
	case CMD_STOP:
		gt->track->cmd((void*)-1, FMED_TRACK_STOPALL);
		break;

	case CMD_PLAY:
		if (gt->curtrk == NULL) {

			if (gt->curtrk_rec != NULL) {
				if (gt->curtrk_rec->paused) {
					gt->curtrk_rec->paused = 0;
					gt->track->cmd(gt->curtrk_rec->trk, FMED_TRACK_UNPAUSE);
					break;
				}
				gt->track->cmd(gt->curtrk_rec->trk, FMED_TRACK_PAUSE);
				fmed_infolog(core, gt->curtrk_rec->trk, "tui", "Recording is paused");
				gt->curtrk_rec->paused = 1;
				break;
			}

			gt->qu->cmd(FMED_QUE_PLAY, NULL);
			break;
		}

		if (gt->curtrk->paused) {
			gt->curtrk->paused = 0;
			gt->curtrk->d->snd_output_pause = 0;
			gt->track->cmd(gt->curtrk->trk, FMED_TRACK_UNPAUSE);
			break;
		}

		gt->curtrk->d->snd_output_pause = 1;
		gt->curtrk->paused = 1;
		break;

	case CMD_NEXT:
		gt->qu->cmdv(FMED_QUE_NEXT2, NULL);
		break;

	case CMD_PREV:
		gt->qu->cmdv(FMED_QUE_PREV2, NULL);
		break;

	case CMD_QUIT:
		gt->track->cmd(NULL, FMED_TRACK_STOPALL_EXIT);
		break;

	case CMD_LIST_RANDOM: {
		int val = gt->qu->cmdv(FMED_QUE_FLIP_RANDOM);
		fmed_infolog(core, NULL, "tui", "Random: %u", val);
		break;
	}
	}
}

void list_save()
{
	char *fn = NULL, *tmpdir = NULL;
	fftime t;
	fftime_now(&t);

#ifdef FF_WIN
	if (NULL == (tmpdir = core->env_expand(NULL, 0, "%TMP%")))
		return;
	fn = ffsz_allocfmt("%s\\fmedia-%U.m3u8", tmpdir, t.sec);
#else
	fn = ffsz_allocfmt("/tmp/fmedia-%U.m3u8", t.sec);
#endif

	gt->qu->fmed_queue_save(-1, fn);
	fmed_infolog(core, NULL, "tui", "Saved playlist to %s", fn);

	ffmem_free(fn);
	ffmem_free(tmpdir);
}

void list_rm_dead()
{
	int r = gt->qu->cmdv(FMED_QUE_RMDEAD, NULL);
	fmed_infolog(core, NULL, "tui", "Removed %u \"dead\" items", r);
}

void trk_rm_playnext()
{
	if (gt->curtrk == NULL)
		return;
	tui_rmfile(gt->curtrk, 0);
	tui_op(CMD_NEXT);
}

struct key {
	uint key;
	uint cmd;
	void *func; // cmdfunc | cmdfunc1
};

static struct key hotkeys[] = {
	{ ' ',	CMD_PLAY | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'D',	_CMD_CURTRK | _CMD_CORE,	file_del },
	{ 'E',	_CMD_F1 | _CMD_CORE,	list_rm_dead },
	{ 'L',	_CMD_F1 | _CMD_CORE,	list_save },
	{ 'T',	CMD_SAVETRK | _CMD_CURTRK | _CMD_CURTRK_REC | _CMD_CORE,	&tui_op_trk },

	{ 'd',	_CMD_CURTRK | _CMD_CORE,	&tui_rmfile },
	{ 'h',	_CMD_F1,	&tui_help },
	{ 'i',	CMD_SHOWTAGS | _CMD_CURTRK | _CMD_CORE,	&tui_op_trk },
	{ 'm',	CMD_MUTE | _CMD_CURTRK | _CMD_CORE,	&tui_vol },
	{ 'n',	CMD_NEXT | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'p',	CMD_PREV | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'q',	CMD_QUIT | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'r',	CMD_LIST_RANDOM | _CMD_F1 | _CMD_CORE,	tui_op },
	{ 's',	CMD_STOP | _CMD_F1 | _CMD_CORE,	&tui_op },
	{ 'x',	_CMD_F1 | _CMD_CORE,	trk_rm_playnext },

	{ FFKEY_UP,	CMD_VOLUP | _CMD_CURTRK | _CMD_CORE,	&tui_vol },
	{ FFKEY_DOWN,	CMD_VOLDOWN | _CMD_CURTRK | _CMD_CORE,	&tui_vol },
	{ FFKEY_RIGHT,	CMD_SEEKRIGHT | _CMD_F3 | _CMD_CORE,	&tui_seek },
	{ FFKEY_LEFT,	CMD_SEEKLEFT | _CMD_F3 | _CMD_CORE,	&tui_seek },
};

static const struct key* key2cmd(int key)
{
	size_t i, start = 0;
	uint k = (key & ~FFKEY_MODMASK), n = FFCNT(hotkeys);
	while (start != n) {
		i = start + (n - start) / 2;
		if (k == hotkeys[i].key) {
			return &hotkeys[i];
		} else if (k < hotkeys[i].key)
			n = i;
		else
			start = i + 1;
	}
	return NULL;
}

struct corecmd {
	fftask tsk;
	const struct key *k;
	void *udata;
};

static void tui_help(uint cmd)
{
	char buf[4096];
	ssize_t n;
	char *fn;
	fffd f;

	if (NULL == (fn = core->getpath(FFSTR("help-tui.txt"))))
		return;

	f = fffile_open(fn, FFO_RDONLY | FFO_NOATIME);
	ffmem_free(fn);
	if (f == FF_BADFD)
		return;
	n = fffile_read(f, buf, sizeof(buf));
	fffile_close(f);
	if (n > 0)
		fffile_write(ffstdout, buf, n);
}

static void tui_corecmd(void *param)
{
	struct corecmd *c = param;

	if (c->k->cmd & _CMD_F1) {
		cmdfunc1 func1 = (void*)c->k->func;
		func1(c->k->cmd & CMD_MASK);

	} else if (c->k->cmd & _CMD_F3) {
		if (gt->curtrk == NULL)
			goto done;
		cmdfunc3 func3 = (void*)c->k->func;
		func3(gt->curtrk, c->k->cmd & CMD_MASK, c->udata);

	} else if (c->k->cmd & (_CMD_CURTRK | _CMD_CURTRK_REC)) {
		cmdfunc func = (void*)c->k->func;
		struct tui *t = NULL;
		if ((c->k->cmd & _CMD_CURTRK) && gt->curtrk != NULL)
			t = gt->curtrk;
		else if ((c->k->cmd & _CMD_CURTRK_REC) && gt->curtrk_rec != NULL)
			t = gt->curtrk_rec;
		if (t == NULL)
			goto done;
		func(t, c->k->cmd & CMD_MASK);
	}

done:
	ffmem_free(c);
}

static void tui_corecmd_add(const struct key *k, void *udata)
{
	struct corecmd *c = ffmem_tcalloc1(struct corecmd);
	if (c == NULL) {
		syserrlog(core, NULL, "tui", "alloc");
		return;
	}
	c->tsk.handler = &tui_corecmd;
	c->tsk.param = c;
	c->udata = udata;
	c->k = k;
	core->task(&c->tsk, FMED_TASK_POST);
}

static void tui_cmdread(void *param)
{
	ffstd_ev ev = {};
	ffstr data = {};

#ifdef FF_UNIX
	// Setting it in FMED_OPEN signal handler sometimes causes problems with --debug in Konsole
	fffile_nonblock(ffstdin, 1);
#endif

	for (;;) {
		if (data.len == 0) {
			int r = ffstd_keyread(ffstdin, &ev, &data);
			if (r == 0)
				break;
			else if (r < 0)
				break;
		}

		ffstr keydata = data;
		uint key = ffstd_keyparse(&data);
		if (key == (uint)-1) {
			data.len = 0;
			continue;
		}

		const struct key *k = key2cmd(key);
		if (k == NULL) {
			dbglog(core, NULL, "tui", "unknown key seq %*xb"
				, (size_t)keydata.len, keydata.ptr);
			continue;
		}
		dbglog(core, NULL, "tui", "received command %u", k->cmd & CMD_MASK);

		if (k->cmd & _CMD_CORE) {
			void *udata = NULL;
			if (k->cmd & _CMD_F3)
				udata = (void*)(size_t)key;
			tui_corecmd_add(k, udata);

		} else if (k->cmd & _CMD_F1) {
			cmdfunc1 func1 = (void*)k->func;
			func1(k->cmd & ~_CMD_F1);
		}
	}

#ifdef FF_UNIX
	fffile_nonblock(ffstdin, 0);
#endif
}
