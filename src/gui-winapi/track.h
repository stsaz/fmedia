/** fmedia: gui-winapi: track filter - a bridge between GUI and Core
2021, Simon Zolin */

void* gtrk_open(fmed_filt *d)
{
	gui_trk *g = ffmem_tcalloc1(gui_trk);
	if (g == NULL)
		return NULL;
	g->lastpos = (uint)-1;
	g->d = d;
	g->trk = d->trk;

	g->qent = (void*)fmed_getval("queue_item");
	if (g->qent == FMED_PNULL) {
		ffmem_free(g);
		return FMED_FILT_SKIP; //tracks being recorded are not started from "queue"
	}

	if (gg->conf.auto_attenuate_ceiling < 0) {
		d->audio.auto_attenuate_ceiling = gg->conf.auto_attenuate_ceiling;
	}

	if (FMED_PNULL != d->track->getvalstr(d->trk, "output"))
		g->conversion = 1;
	else {
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

	if (d->meta_block)
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

	if (g->goback) {
		g->goback = 0;
		return FMED_RMORE;
	}
	if (g->d->snd_output_clear_wait) {
		if ((int64)d->audio.seek != FMED_NULL)
			return FMED_RMORE; // decoder must complete our seek request
		g->d->snd_output_clear_wait = 0;
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
	return FMED_ROK;
}

//GUI-TRACK
const fmed_filter fmed_gui = {
	gtrk_open, gtrk_process, gtrk_close
};
