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

	uint time_cur;
	uint time_total;
	int time_seek;
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
	gtrk *t = gg->curtrk;
	int delta;
	if (t == NULL)
		return;

	switch (cmd) {
	case A_SEEK:
		t->time_seek = val;
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
		t->time_seek = ffmax((int)t->time_cur + delta, 0);
		break;
	}
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
	t->time_seek = -1;
	t->qent = ent;
	t->d = d;

	if (FMED_PNULL != d->track->getvalstr(d->trk, "output")) {
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

	if (d->meta_block)
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

	if (t->time_seek != -1) {
		d->audio.seek = t->time_seek * 1000;
		d->snd_output_clear = 1;
		t->time_seek = -1;
		return FMED_RMORE;
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
	return FMED_ROK;
}

// GUI-TRACK
static const fmed_filter gui_track_iface = {
	&gtrk_open, &gtrk_process, &gtrk_close
};
