/** fmedia: entry-track relations
2020, Simon Zolin */

static void que_ontrkfin(entry *e);
static void que_trk_close(void *ctx);

enum CMD {
	CMD_TRKFIN = 0x010000,
	CMD_TRKFIN_EXPAND,
};

struct quetask {
	uint cmd; //enum FMED_QUE or enum CMD
	size_t param;
	fftask tsk;
	ffchain_item sib;
};

static void que_taskfunc(void *udata)
{
	struct quetask *qt = udata;
	qt->tsk.handler = NULL;
	switch ((enum CMD)qt->cmd) {
	case CMD_TRKFIN:
		que_ontrkfin((void*)qt->param);
		break;

	case CMD_TRKFIN_EXPAND: {
		entry *e = (void*)qt->param;
		pl_expand_next(e->plist, e);
		ent_unref(e);
		break;
	}

	default:
		que_cmd(qt->cmd, (void*)qt->param);
	}
	ffmem_free(qt);
}

static void que_task_add(struct quetask *qt)
{
	qt->tsk.handler = &que_taskfunc;
	qt->tsk.param = qt;
	core->task(&qt->tsk, FMED_TASK_POST);
}


static void* que_expand2(entry *e, void *ondone, void *ctx)
{
	void *trk = qu->track->create(FMED_TRK_TYPE_EXPAND, e->e.url.ptr);
	if (trk == NULL || trk == FMED_TRK_EFMT) {
		return trk;
	}
	fmed_trk *t = qu->track->conf(trk);
	if (e->trk != NULL)
		qu->track->copy_info(t, e->trk);
	qu->track->setval(trk, "queue-ondone", (int64)(size_t)ondone);
	qu->track->setval(trk, "queue-ondone-ctx", (int64)(size_t)ctx);
	ent_ref(e); // for user to be able to get the next element in the list
	ent_start_prepare(e, trk);
	qu->track->cmd(trk, FMED_TRACK_START);
	return trk;
}

/** Start multiple tracks. */
static void que_xplay(entry *e)
{
	for (;;) {
		e->plist->xcursor = e;
		ffbool last = (e->sib.next == fflist_sentl(&e->plist->ents));
		que_play2(e, 1);
		if (0 == core->cmd(FMED_WORKER_AVAIL))
			break;
		if (last)
			break;
		e = FF_GETPTR(entry, sib, e->sib.next);
	}
}

static void que_play(entry *e)
{
	if (e->plist->parallel) {
		e = e->plist->xcursor;
		if (e == NULL || e->sib.next == fflist_sentl(&e->plist->ents))
			return;
		e = FF_GETPTR(entry, sib, e->sib.next);
		que_xplay(e);
		return;
	}

	que_play2(e, 0);
}

static int have_output(entry *ent)
{
	const ffstr *dict = ent->dict.ptr;
	for (uint i = 0;  i != ent->dict.len;  i += 2) {
		ffstr k = dict[i], v = dict[i + 1];
		if ((ffssize)v.len >= 0)
			if (ffstr_eqz(&k, "output"))
				return 1;
	}
	return 0;
}

static void que_play2(entry *ent, uint flags)
{
	fmed_que_entry *e = &ent->e;
	int type = FMED_TRK_TYPE_PLAYBACK;
	if (ent->trk != NULL && ent->trk->pcm_peaks)
		type = FMED_TRK_TYPE_PCMINFO;
	else if (qu->mixing)
		type = FMED_TRK_TYPE_MIXIN;
	else if ((flags & 1) || have_output(ent))
		type = FMED_TRK_TYPE_CONVERT;
	void *trk = qu->track->create(type, e->url.ptr);
	uint i;

	if (trk == NULL)
		return;
	else if (trk == FMED_TRK_EFMT) {
		entry *next;
		if (NULL != (next = que_getnext(ent))) {
			struct quetask *qt = ffmem_new(struct quetask);
			FF_ASSERT(qt != NULL);
			qt->cmd = FMED_QUE_PLAY;
			qt->param = (size_t)next;
			que_task_add(qt);
		}

		que_cmd(FMED_QUE_RM, e);
		return;
	}

	fmed_trk *t = qu->track->conf(trk);
	if (ent->trk != NULL)
		qu->track->copy_info(t, ent->trk);

	if (e->dur != 0)
		qu->track->setval(trk, "track_duration", e->dur);
	if (e->from != 0)
		t->audio.abs_seek = e->from;
	if (e->to != 0 && FMED_NULL == t->audio.until)
		t->audio.until = e->to - e->from;

	const ffstr *dict = ent->dict.ptr;
	for (i = 0;  i != ent->dict.len;  i += 2) {
		if ((ssize_t)dict[i + 1].len >= 0)
			qu->track->setvalstr(trk, dict[i].ptr, dict[i + 1].ptr);
		else
			qu->track->setval(trk, dict[i].ptr, *(int64*)dict[i + 1].ptr); //FMED_QUE_NUM
	}

	const char *smeta = qu->track->getvalstr(trk, "meta");
	if (smeta != FMED_PNULL && 0 != que_setmeta(ent, smeta, trk)) {
		que_cmd(FMED_QUE_RM, e);
		return;
	}

	ent_start_prepare(ent, trk);
	if (flags & 1)
		qu->track->cmd(trk, FMED_TRACK_XSTART);
	else
		qu->track->cmd(trk, FMED_TRACK_START);
}

static void que_mix(void)
{
	fflist *ents = &qu->curlist->ents;
	void *mxout;
	entry *e;

	if (NULL == (mxout = qu->track->create(FMED_TRK_TYPE_MIXOUT, NULL)))
		return;
	qu->track->setval(mxout, "mix_tracks", ents->len);
	qu->track->cmd(mxout, FMED_TRACK_START);

	qu->mixing = 1;
	_FFLIST_WALK(ents, e, sib) {
		que_play(e);
	}
}


typedef struct que_trk {
	entry *e;
	const fmed_track *track;
	void *trk;
	fmed_filt *d;
} que_trk;

static void* que_trk_open(fmed_filt *d)
{
	que_trk *t;
	entry *e = (void*)d->track->getval(d->trk, "queue_item");

	if ((int64)e == FMED_NULL)
		return FMED_FILT_SKIP; //the track wasn't created by this module

	t = ffmem_tcalloc1(que_trk);
	if (t == NULL) {
		ent_unref(e);
		return NULL;
	}
	t->track = d->track;
	t->trk = d->trk;
	t->e = e;
	t->d = d;

	if (1 == fmed_getval("error")) {
		que_trk_close(t);
		return NULL;
	}
	return t;
}

static void que_trk_close(void *ctx)
{
	que_trk *t = ctx;
	entry *e = t->e;

	if ((int64)t->d->audio.total != FMED_NULL && t->d->audio.fmt.sample_rate != 0)
		t->e->e.dur = ffpcm_time(t->d->audio.total, t->d->audio.fmt.sample_rate);

	int err = t->track->getval(t->trk, "error");
	t->e->trk_stopped = !!(t->d->flags & FMED_FSTOP);
	if (t->d->type == FMED_TRK_TYPE_EXPAND && !e->plist->expand_all)
		e->trk_stopped = 1;
	t->e->trk_err = (err != FMED_NULL);

	struct quetask *qt = ffmem_new(struct quetask);
	qt->cmd = CMD_TRKFIN;
	if (t->d->type == FMED_TRK_TYPE_EXPAND && e->plist->expand_all)
		qt->cmd = CMD_TRKFIN_EXPAND;
	qt->param = (size_t)t->e;
	que_task_add(qt);

	if (t->d->type == FMED_TRK_TYPE_EXPAND && qu->onchange != NULL)
		qu->onchange(&e->e, FMED_QUE_ONUPDATE);

	int64 v = t->track->getval(t->trk, "queue-ondone");
	if (v != FMED_NULL) {
		void (*ondone)(void*) = (void*)(size_t)v;
		v = t->track->getval(t->trk, "queue-ondone-ctx");
		FF_ASSERT(v != FMED_NULL);
		void *ctx = (void*)(size_t)v;
		ondone(ctx);
	}

	ffmem_free(t);
}

static int que_trk_process(void *ctx, fmed_filt *d)
{
	d->outlen = 0;
	return FMED_RDONE;
}

/** Called after a track has been finished.
Thread: main */
static void que_ontrkfin(entry *e)
{
	if (!e->trk_err && e->plist->nerrors != 0)
		e->plist->nerrors = 0;

	if (qu->mixing) {
		qu->track->cmd(NULL, FMED_TRACK_LAST);
	} else if (e->stop_after)
		e->stop_after = 0;
	else if (e->trk_stopped)
	{}
	else if (!e->trk_err || qu->next_if_err) {
		if (qu->random || qu->repeat == FMED_QUE_REPEAT_ALL) {
			/* Don't start the next track when there are too many consecutive errors.
			When in Random or Repeat-all mode we may waste CPU resources
			 without making any progress, e.g.:
			. input: storage isn't online, files been moved, etc.
			. filters: format or codec isn't supported, decoding error, etc.
			. output: audio system is failing */

			if (e->trk_err && ++e->plist->nerrors == MAX_N_ERRORS) {
				infolog("Stopping playback: too many consecutive errors");
				e->plist->nerrors = 0;
				goto end;
			}
		}
		que_cmd(FMED_QUE_NEXT2, &e->e);
	}

end:
	ent_unref(e);
}

static const fmed_filter fmed_que_trk = {
	que_trk_open, que_trk_process, que_trk_close
};
