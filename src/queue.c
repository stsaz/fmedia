/** Track queue.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>


static const fmed_core *core;

typedef struct entry {
	fmed_que_entry e;
	fflist_item sib;
} entry;

typedef struct que {
	fflock lk;
	fflist list; //entry[]
	entry *cur;
	const fmed_track *track;
	fmed_que_onchange_t onchange;

	fftask tsk;
	uint tsk_cmd;
	void *tsk_param;

	uint quit_if_done :1;
} que;

static que *qu;


//FMEDIA MODULE
static const void* que_iface(const char *name);
static int que_sig(uint signo);
static void que_destroy(void);
static const fmed_mod fmed_que_mod = {
	&que_iface, &que_sig, &que_destroy
};

static fmed_que_entry* que_add(fmed_que_entry *ent);
static fmed_que_entry* que_get(void);
static void que_cmd(uint cmd, void *param);
static const fmed_queue fmed_que_mgr = {
	&que_add, &que_get, &que_cmd
};

static void que_play(void);
static void ent_free(entry *e);
static void que_taskfunc(void *udata);
static void que_task_add(uint cmd);

//QUEUE-TRACK
static void* que_trk_open(fmed_filt *d);
static int que_trk_process(void *ctx, fmed_filt *d);
static void que_trk_close(void *ctx);
static const fmed_filter fmed_que_trk = {
	&que_trk_open, &que_trk_process, &que_trk_close
};


const fmed_mod* fmed_getmod_queue(const fmed_core *_core)
{
	core = _core;
	return &fmed_que_mod;
}


static const void* que_iface(const char *name)
{
	if (!ffsz_cmp(name, "track"))
		return &fmed_que_trk;
	else if (!ffsz_cmp(name, "queue"))
		return (void*)&fmed_que_mgr;
	return NULL;
}

static int que_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (qu = ffmem_tcalloc1(que)))
			return 1;
		fflist_init(&qu->list);
		qu->track = core->getmod("#core.track");
		if (1 != core->getval("gui"))
			qu->quit_if_done = 1;

		qu->tsk.handler = &que_taskfunc;
		break;
	}
	return 0;
}

static void ent_free(entry *e)
{
	uint i;
	for (i = 0;  i < e->e.nmeta;  i++) {
		ffstr_free(&e->e.meta[i]);
	}
	ffmem_safefree(e->e.meta);
	ffstr_free(&e->e.url);
	ffmem_free(e);
}

static void que_destroy(void)
{
	if (qu == NULL)
		return;
	core->task(&qu->tsk, FMED_TASK_DEL);
	FFLIST_ENUMSAFE(&qu->list, ent_free, entry, sib);
	ffmem_free(qu);
}


static void que_play(void)
{
	fmed_que_entry *e = &qu->cur->e;
	void *trk = qu->track->create(FMED_TRACK_OPEN, e->url.ptr);
	uint i;
	if (trk == NULL)
		return;
	if (e->dur != 0)
		qu->track->setval(trk, "track_duration", e->dur);
	if (e->from != 0)
		qu->track->setval(trk, "seek_time_abs", e->from);
	if (e->to != 0)
		qu->track->setval(trk, "until_time", e->to);
	for (i = 0;  i != e->nmeta;  i += 2) {
		qu->track->setvalstr(trk, e->meta[i].ptr, e->meta[i + 1].ptr);
	}
	qu->track->setval(trk, "queue_item", (int64)e);
	qu->track->cmd(trk, FMED_TRACK_START);
}

static entry* que_getnext(entry *from)
{
	if (qu->list.len == 0)
		goto nonext;

	if (from == NULL) {
		return FF_GETPTR(entry, sib, qu->list.first);

	} else if (from->sib.next == fflist_sentl(&qu->list)) {
		if (1 != core->getval("repeat_all")) {
nonext:
			dbglog(core, NULL, "que", "no next file in playlist");
			if (qu->quit_if_done)
				core->sig(FMED_STOP);
			return NULL;
		}

		dbglog(core, NULL, "que", "repeat_all: starting from the beginning");
		return FF_GETPTR(entry, sib, qu->list.first);
	}

	return FF_GETPTR(entry, sib, from->sib.next);
}

// matches enum FMED_QUE
static const char *const scmds[] = {
	"play", "next", "prev", "clear", "rm", "setonchange",
};

static void que_cmd(uint cmd, void *param)
{
	entry *e;

	dbglog(core, NULL, "que", "received command:%s, param:%p", scmds[cmd], param);

	//@ fflk_lock(&qu->lk);
	switch (cmd) {
	case FMED_QUE_PLAY:
		if (qu->list.len == 0)
			break;

		if (param != NULL) {
			qu->cur = param;

		} else if (qu->cur == NULL)
			qu->cur = FF_GETPTR(entry, sib, qu->list.first);
		que_play();
		break;

	case FMED_QUE_NEXT:
		qu->cur = que_getnext(qu->cur);
		que_play();
		break;

	case FMED_QUE_PREV:
		if (qu->cur == NULL || qu->cur->sib.prev == fflist_sentl(&qu->list)) {
			qu->cur = NULL;
			dbglog(core, NULL, "que", "no previous file in playlist");
			break;
		}
		qu->cur = FF_GETPTR(entry, sib, qu->cur->sib.prev);
		que_play();
		break;

	case FMED_QUE_RM:
		e = param;
		fflist_rm(&qu->list, &e->sib);
		if (qu->cur == e)
			qu->cur = NULL;
		dbglog(core, NULL, "que", "removed item %S", &e->e.url);

		if (qu->onchange != NULL)
			qu->onchange(&e->e, FMED_QUE_ONRM);

		ent_free(e);
		break;

	case FMED_QUE_CLEAR:
		qu->cur = NULL;
		FFLIST_ENUMSAFE(&qu->list, ent_free, entry, sib);
		fflist_init(&qu->list);
		dbglog(core, NULL, "que", "cleared");
		break;

	case FMED_QUE_SETONCHANGE:
		qu->onchange = param;
		break;
	}
	// fflk_unlock(&qu->lk);
}

static fmed_que_entry* que_get(void)
{
	if (qu->cur == NULL)
		return NULL;
	return &qu->cur->e;
}

static fmed_que_entry* que_add(fmed_que_entry *ent)
{
	uint i;
	entry *e = ffmem_tcalloc1(entry);
	if (e == NULL)
		return NULL;

	if (NULL == (e->e.url.ptr = ffsz_alcopy(ent->url.ptr, ent->url.len))
		|| (ent->nmeta != 0 && NULL == (e->e.meta = ffmem_talloc(ffstr, ent->nmeta)))) {
		ent_free(e);
		return NULL;
	}
	e->e.url.len = ffsz_len(e->e.url.ptr);

	if (ent->nmeta % 2) {
		ent_free(e);
		return NULL;
	}

	for (i = 0;  i < ent->nmeta;  i++) {
		if (NULL == (e->e.meta[i].ptr = ffsz_alcopy(ent->meta[i].ptr, ent->meta[i].len))) {
			ent_free(e);
			return NULL;
		}
		e->e.meta[i].len = ffsz_len(e->e.meta[i].ptr);
		e->e.nmeta++;
	}

	e->e.from = ent->from;
	e->e.to = ent->to;
	e->e.dur = ent->dur;

	fflk_lock(&qu->lk);
	fflist_ins(&qu->list, &e->sib);
	fflk_unlock(&qu->lk);

	dbglog(core, NULL, "que", "added: (%d: %d-%d) %S"
		, ent->dur, ent->from, ent->to, &ent->url);

	if (qu->onchange != NULL)
		qu->onchange(&e->e, FMED_QUE_ONADD);
	return &e->e;
}

static void que_taskfunc(void *udata)
{
	void *param = qu->tsk_param;
	qu->tsk_param = NULL;
	que_cmd(qu->tsk_cmd, param);
}

static void que_task_add(uint cmd)
{
	qu->tsk_cmd = cmd;
	core->task(&qu->tsk, FMED_TASK_POST);
}


typedef struct que_trk {
	entry *e;
	const fmed_track *track;
	void *trk;
} que_trk;

static void* que_trk_open(fmed_filt *d)
{
	que_trk *t;
	entry *e = (void*)d->track->getval(d->trk, "queue_item");

	if ((int64)e == FMED_NULL)
		return FMED_FILT_SKIP; //the track wasn't created by this module

	t = ffmem_tcalloc1(que_trk);
	if (t == NULL)
		return NULL;
	t->track = d->track;
	t->trk = d->trk;
	t->e = e;

	if (1 == fmed_getval("error")) {
		que_trk_close(t);
		return NULL;
	}
	return t;
}

static void que_trk_close(void *ctx)
{
	que_trk *t = ctx;
	entry *next = NULL;

	if (qu->quit_if_done
		|| 1 != t->track->getval(t->trk, "stopped"))
		next = que_getnext(t->e);

	//delete the item from queue if there was an error
	if (FMED_NULL != t->track->getval(t->trk, "error"))
		que_cmd(FMED_QUE_RM, t->e);

	if (next != NULL) {
		qu->tsk_param = next;
		que_task_add(FMED_QUE_PLAY);
	}

	ffmem_free(t);
}

static int que_trk_process(void *ctx, fmed_filt *d)
{
	d->outlen = 0;
	return FMED_RDONE;
}
