/** fmedia: gui-winapi: track filter - a bridge between GUI and Core
2021, Simon Zolin */

void gtrk_seek(uint pos_sec)
{
	if (gg->curtrk == NULL)
		return;
	gui_trk *t = gg->curtrk;

	t->seek_msec = pos_sec * 1000;
	t->d->seek_req = 1;
	fmed_dbglog(core, t->d->trk, "gui", "seek: %U", t->seek_msec);
	if (t->d->adev_ctx != NULL)
		t->d->adev->cmd(FMED_ADEV_CMD_CLEAR, t->d->adev_ctx);
}

void* gtrk_open(fmed_filt *d)
{
	gui_trk *g = ffmem_tcalloc1(gui_trk);
	if (g == NULL)
		return NULL;
	g->lastpos = (uint)-1;
	g->d = d;
	g->trk = d->trk;
	g->seek_msec = -1;

	g->qent = (void*)fmed_getval("queue_item");
	if (g->qent == FMED_PNULL) {
		ffmem_free(g);
		return FMED_FILT_SKIP; //tracks being recorded are not started from "queue"
	}

	if (gg->conf.auto_attenuate_ceiling < 0) {
		d->audio.auto_attenuate_ceiling = gg->conf.auto_attenuate_ceiling;
	}

	if (d->type == FMED_TRK_TYPE_CONVERT) {
		g->conversion = 1;
	} else if (d->type == FMED_TRK_TYPE_PLAYBACK) {
		fflk_lock(&gg->lktrk);
		gg->curtrk = g;
		fflk_unlock(&gg->lktrk);

		gui_corecmd_op(A_PLAY_VOL, NULL);
	}

	g->state = ST_PLAYING;
	return g;
}

void gtrk_close(void *ctx)
{
	gui_trk *g = ctx;
	gui_setmeta(NULL, g->qent);

	if (gg->curtrk == g) {
		fflk_lock(&gg->lktrk);
		gg->curtrk = NULL;
		fflk_unlock(&gg->lktrk);
		gui_clear();
	}
	ffmem_free(g);
}

int gtrk_process(void *ctx, fmed_filt *d)
{
	gui_trk *g = ctx;
	int64 playpos;
	uint playtime, first = 0;

	if (d->input_info)
		return FMED_RFIN;

	if (d->meta_block && (d->flags & FMED_FFWD))
		goto done;

	if (g->sample_rate == 0) {
		if (d->audio.fmt.format == 0) {
			errlog(core, d->trk, NULL, "audio format isn't set");
			return FMED_RERR;
		}

		g->sample_rate = d->audio.fmt.sample_rate;
		g->total_time_sec = ffpcm_time(d->audio.total, g->sample_rate) / 1000;
		first = 1;
	}

	if (g->seek_msec != -1) {
		d->audio.seek = g->seek_msec;
		g->seek_msec = -1;
		return FMED_RMORE; // new seek request
	} else if (!(d->flags & FMED_FFWD)) {
		return FMED_RMORE; // going back without seeking
	} else if (d->data_in.len == 0 && !(d->flags & FMED_FLAST)) {
		return FMED_RMORE; // waiting for audio data before resetting seek position
	} else if ((int64)d->audio.seek != FMED_NULL && !d->seek_req) {
		fmed_dbglog(core, d->trk, NULL, "seek: done");
		d->audio.seek = FMED_NULL; // prev. seek is complete
	}

	if (g->conversion) {
		playtime = (uint)(ffpcm_time(d->audio.pos, g->sample_rate) / 1000);
		if (playtime == g->lastpos && !(d->flags & FMED_FLAST))
			goto done;
		g->lastpos = playtime;
		if (d->flags & FMED_FLAST)
			playtime = (uint)-1;
		wmain_convert_progress(g->qent, playtime, g->total_time_sec);
		goto done;
	}

	if (first) {
		gui_newtrack(g, d, g->qent);
		d->meta_changed = 0;
	}

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if (d->meta_changed) {
		d->meta_changed = 0;
		gui_setmeta(g, g->qent);
	}

	playpos = d->audio.pos;
	if (playpos == FMED_NULL) {
		goto done;
	}

	playtime = (uint)(ffpcm_time(playpos, g->sample_rate) / 1000);
	if (playtime == g->lastpos)
		goto done;
	g->lastpos = playtime;

	wmain_update(playtime, g->total_time_sec);

done:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return (d->type == FMED_TRK_TYPE_PLAYBACK) ? FMED_RDATA : FMED_ROK;
}

//GUI-TRACK
const fmed_filter fmed_gui = {
	gtrk_open, gtrk_process, gtrk_close
};
