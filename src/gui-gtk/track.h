/** fmedia: gui-gtk: track filter - a bridge between GUI and Core
2019,2021, Simon Zolin */

/* public:
gtrk_seek
gtrk_vol
*/

typedef struct gtrk {
	void *trk;
	fmed_que_entry *qent;
	fmed_filt *d;
	uint sample_rate;

	int64 seek_msec;
	uint time_cur;
	uint time_total;
	uint last_timer_val;

	uint paused :1;
	uint have_fmt :1;
	uint conversion :1;
} gtrk;

static double gtrk_vol2(gtrk *t, uint pos)
{
	double db;
	if (pos <= 100)
		db = ffpcm_vol2db(pos, 48);
	else
		db = ffpcm_vol2db_inc(pos - 100, 25, 6);
	t->d->audio.gain = db * 100;
	return db;
}

/** Set volume. */
static void gtrk_vol(uint pos)
{
	if (gg->curtrk == NULL)
		return;

	double db = gtrk_vol2(gg->curtrk, pos);
	wmain_status("Volume: %.02FdB", db);
}

/** Set seek position. */
static void gtrk_seek(uint cmd, uint val)
{
	if (gg->curtrk == NULL)
		return;
	gtrk *t = gg->curtrk;
	int delta, seek = -1;
	switch (cmd) {
	case A_SEEK:
		seek = val;
		break;

	case A_FFWD:
		delta = gg->conf.seek_step_delta;
		goto use_delta;
	case A_RWND:
		delta = -(int)gg->conf.seek_step_delta;
		goto use_delta;

	case A_LEAP_FWD:
		delta = gg->conf.seek_leap_delta;
		goto use_delta;
	case A_LEAP_BACK:
		delta = -(int)gg->conf.seek_leap_delta;
		goto use_delta;

use_delta:
		seek = ffmax((int)t->time_cur + delta, 0);
		break;
	}

	if (seek < 0)
		return;

	t->seek_msec = seek * 1000;
	t->d->seek_req = 1;
	fmed_dbglog(core, t->d->trk, "gui", "seek: %U", t->seek_msec);
	if (t->d->adev_ctx != NULL)
		t->d->adev->cmd(FMED_ADEV_CMD_CLEAR, t->d->adev_ctx);
}

static void* gtrk_open(fmed_filt *d)
{
	fmed_que_entry *ent = (void*)d->track->getval(d->trk, "queue_item");
	if (ent == FMED_PNULL)
		return FMED_FILT_SKIP;

	gtrk *t = ffmem_new(gtrk);
	if (t == NULL)
		return NULL;
	t->time_cur = -1;
	t->qent = ent;
	t->d = d;
	t->seek_msec = -1;

	if (d->type == FMED_TRK_TYPE_CONVERT) {
		t->conversion = 1;
		gui_timer_start();
	} else {
		if (gg->vol != 100)
			gtrk_vol2(t, gg->vol);

		if (gg->conf.auto_attenuate_ceiling < 0) {
			d->audio.auto_attenuate_ceiling = gg->conf.auto_attenuate_ceiling;
		}
		gg->curtrk = t;
	}

	t->trk = d->trk;
	return t;
}

static void gtrk_close(void *ctx)
{
	gtrk *t = ctx;
	if (t->conversion) {
		gui_timer_stop();

	} else if (gg->curtrk == t) {
		gg->curtrk = NULL;
		wmain_fintrack();
	}
	ffmem_free(t);
}

static int gtrk_process(void *ctx, fmed_filt *d)
{
	gtrk *t = ctx;

	if (d->meta_block && (d->flags & FMED_FFWD))
		goto done;

	if (!t->have_fmt) {
		t->have_fmt = 1;
		if (d->audio.fmt.format == 0) {
			errlog("audio format isn't set");
			return FMED_RERR;
		}

		t->sample_rate = d->audio.fmt.sample_rate;
		t->time_total = ffpcm_time(d->audio.total, t->sample_rate) / 1000;
		wmain_newtrack(t->qent, t->time_total, d);
		d->meta_changed = 0;
	}

	if ((int64)d->audio.pos == FMED_NULL)
		goto done;

	if (t->seek_msec != -1) {
		d->audio.seek = t->seek_msec;
		t->seek_msec = -1;
		return FMED_RMORE; // new seek request
	} else if (!(d->flags & FMED_FFWD)) {
		return FMED_RMORE; // going back without seeking
	} else if (d->data_in.len == 0 && !(d->flags & FMED_FLAST)) {
		return FMED_RMORE; // waiting for audio data before resetting seek position
	} else if ((int64)d->audio.seek != FMED_NULL && !d->seek_req) {
		fmed_dbglog(core, d->trk, NULL, "seek: done");
		d->audio.seek = FMED_NULL; // prev. seek is complete
	}

	uint playtime = (uint)(ffpcm_time(d->audio.pos, t->sample_rate) / 1000);
	if (playtime == t->time_cur && !(d->flags & FMED_FLAST))
		goto done;
	t->time_cur = playtime;

	if (t->conversion) {
		if (d->flags & FMED_FLAST)
			playtime = (uint)-1;
		else if (t->last_timer_val == gg->timer_val)
			goto done; // don't update too frequently
		t->last_timer_val = gg->timer_val;

		wmain_update_convert(t->qent, playtime, t->time_total);
		goto done;
	}

	if (d->meta_changed) {
		d->meta_changed = 0;
		wmain_newtrack(t->qent, t->time_total, d);
	}

	if (t == gg->curtrk)
		wmain_update(playtime, t->time_total);

done:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return (d->type == FMED_TRK_TYPE_PLAYBACK) ? FMED_RDATA : FMED_ROK;
}

// GUI-TRACK
static const fmed_filter gui_track_iface = {
	&gtrk_open, &gtrk_process, &gtrk_close
};
