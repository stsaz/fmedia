/** Track queue.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <FF/list.h>
#include <FF/data/m3u.h>
#include <FFOS/dir.h>
#include <FFOS/random.h>


#undef syserrlog
#define dbglog0(...)  fmed_dbglog(core, NULL, "que", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "que", __VA_ARGS__)


/*
Metadata priority:
  . from user (--meta)
    if "--meta=clear" is used, skip transient meta
  . from .cue
  . from file or ICY server (transient)
  . artist/title from .m3u (used as transient due to lower priority)
*/

static const fmed_core *core;

typedef struct plist plist;

typedef struct entry {
	fmed_que_entry e;
	fflist_item sib;

	plist *plist;
	fmed_trk *trk;
	ffarr2 meta; //ffstr[]
	ffarr2 tmeta; //ffstr[]. transient meta
	ffarr2 dict; //ffstr[]

	size_t list_pos; //position number within playlist.  May be invalid.
	uint refcount;
	uint rm :1
		, stop_after :1
		, no_tmeta :1
		, expand :1
		, trk_stopped :1
		, trk_err :1
		, trk_mixed :1
		;

	char url[0];
} entry;

struct plist {
	fflist_item sib;
	fflist ents; //entry[]
	ffarr indexes; //entry*[]  Get an entry by its number;  find a number by an entry pointer.
	entry *cur, *xcursor;
	struct plist *filtered_plist; //list with the filtered tracks
	uint rm :1;
	uint allow_random :1;
	uint filtered :1;
	uint parallel :1; // every item in this queue will start via FMED_TRACK_XSTART
};

static void plist_free(plist *pl);
static ssize_t plist_ent_idx(plist *pl, entry *e);
static struct entry* plist_ent(struct plist *pl, size_t idx);

struct que_conf {
	byte next_if_err;
};

typedef struct que {
	fflist plists; //plist[]
	plist *curlist;
	const fmed_track *track;
	fmed_que_onchange_t onchange;
	fflock plist_lock;

	struct que_conf conf;
	uint list_random;
	uint quit_if_done :1
		, next_if_err :1
		, fmeta_lowprio :1 //meta from file has lower priority
		, rnd_ready :1
		, repeat_all :1
		, random :1
		, mixing :1;
} que;

static que *qu;


//FMEDIA MODULE
static const void* que_iface(const char *name);
static int que_mod_conf(const char *name, ffpars_ctx *ctx);
static int que_sig(uint signo);
static void que_destroy(void);
static const fmed_mod fmed_que_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&que_iface, &que_sig, &que_destroy, &que_mod_conf
};

static ssize_t que_cmdv(uint cmd, ...);
static ssize_t que_cmd2(uint cmd, void *param, size_t param2);
static fmed_que_entry* _que_add(fmed_que_entry *ent);
static void que_cmd(uint cmd, void *param);
static void _que_meta_set(fmed_que_entry *ent, const char *name, size_t name_len, const char *val, size_t val_len, uint flags);
static ffstr* que_meta_find(fmed_que_entry *ent, const char *name, size_t name_len);
static ffstr* que_meta(fmed_que_entry *ent, size_t n, ffstr *name, uint flags);
static const fmed_queue fmed_que_mgr = {
	&que_cmdv, &que_cmd, &que_cmd2, &_que_add, &_que_meta_set, &que_meta_find, &que_meta
};

static fmed_que_entry* que_add(plist *pl, fmed_que_entry *ent, entry *prev, uint flags);
static void que_meta_set(fmed_que_entry *ent, const ffstr *name, const ffstr *val, uint flags);
static void que_play(entry *e);
static void que_play2(entry *ent, uint flags);
static void que_save(entry *first, const fflist_item *sentl, const char *fn);
static void ent_rm(entry *e);
static void ent_free(entry *e);
static void que_taskfunc(void *udata);
static void rnd_init();
enum CMD {
	CMD_TRKFIN = 0x010000,
};
struct quetask {
	uint cmd; //enum FMED_QUE or enum CMD
	size_t param;
	fftask tsk;
	ffchain_item sib;
};
static void que_task_add(struct quetask *qt);
static void que_mix(void);
static entry* que_getnext(entry *from);

//QUEUE-TRACK
static void* que_trk_open(fmed_filt *d);
static int que_trk_process(void *ctx, fmed_filt *d);
static void que_trk_close(void *ctx);
static const fmed_filter fmed_que_trk = {
	&que_trk_open, &que_trk_process, &que_trk_close
};
static const ffpars_arg que_conf_args[] = {
	{ "next_if_error",	FFPARS_TBOOL8,  FFPARS_DSTOFF(struct que_conf, next_if_err) },
};
static int que_config(ffpars_ctx *ctx)
{
	qu->conf.next_if_err = 1;
	ffpars_setargs(ctx, &qu->conf, que_conf_args, FFCNT(que_conf_args));
	return 0;
}


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

static int que_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "track"))
		return que_config(ctx);
	return -1;
}

static int que_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		if (NULL == (qu = ffmem_tcalloc1(que)))
			return 1;
		fflist_init(&qu->plists);
		fflk_init(&qu->plist_lock);
		break;

	case FMED_OPEN:
		que_cmd2(FMED_QUE_NEW, NULL, 0);
		que_cmd2(FMED_QUE_SEL, (void*)0, 0);
		qu->track = core->getmod("#core.track");
		qu->next_if_err = qu->conf.next_if_err;
		break;
	}
	return 0;
}

static void ent_rm(entry *e)
{
	if (!e->rm) {
		ssize_t i = plist_ent_idx(e->plist, e);
		if (i >= 0)
			_ffarr_rmshift_i(&e->plist->indexes, i, 1, sizeof(entry*));

		if (e->plist->filtered_plist != NULL) {
			i = plist_ent_idx(e->plist->filtered_plist, e);
			if (i >= 0)
				_ffarr_rmshift_i(&e->plist->filtered_plist->indexes, i, 1, sizeof(entry*));
		}
	}

	if (e->refcount != 0) {
		e->rm = 1;
		return;
	}

	if (e->plist->cur == e)
		e->plist->cur = NULL;
	if (e->plist->xcursor == e)
		e->plist->xcursor = NULL;
	fflist_rm(&e->plist->ents, &e->sib);
	if (e->plist->ents.len == 0 && e->plist->rm)
		plist_free(e->plist);
	ent_free(e);
}

static void ent_ref(entry *e)
{
	e->refcount++;
}

static void ent_unref(entry *e)
{
	FF_ASSERT(e->refcount != 0);
	if (--e->refcount == 0 && e->rm)
		ent_rm(e);
}

static void ent_free(entry *e)
{
	FFARR2_FREE_ALL(&e->meta, ffstr_free, ffstr);
	FFARR2_FREE_ALL(&e->dict, ffstr_free, ffstr);
	FFARR2_FREE_ALL(&e->tmeta, ffstr_free, ffstr);

	ffmem_free(e->trk);
	ffmem_free(e);
}

static void plist_free(plist *pl)
{
	if (pl == NULL)
		return;
	FFLIST_ENUMSAFE(&pl->ents, ent_free, entry, sib);
	ffarr_free(&pl->indexes);
	plist_free(pl->filtered_plist);
	ffmem_free(pl);
}

/** Find a number by an entry pointer. */
static ssize_t plist_ent_idx(plist *pl, entry *e)
{
	int update = 0;
	if (!pl->filtered) {
		entry *e2 = plist_ent(pl, e->list_pos);
		if (e == e2)
			return e->list_pos;
		update = 1;
	}

	entry **p;
	FFARR_WALKT(&pl->indexes, p, entry*) {
		if (*p == e) {
			if (update)
				e->list_pos = p - (entry**)pl->indexes.ptr;
			return p - (entry**)pl->indexes.ptr;
		}
	}
	return -1;
}

/** Get an entry pointer by its index. */
static struct entry* plist_ent(struct plist *pl, size_t idx)
{
	if (idx >= pl->indexes.len)
		return NULL;
	struct entry *e = ((entry**)pl->indexes.ptr) [idx];
	return e;
}


static void que_destroy(void)
{
	if (qu == NULL)
		return;
	FFLIST_ENUMSAFE(&qu->plists, plist_free, plist, sib);
	ffmem_free0(qu);
}


/**
@meta: string of format "[clear;]NAME=VAL;NAME=VAL..." */
static int que_setmeta(entry *ent, const char *meta, void *trk)
{
	int rc = -1, f;
	ffarr buf = {};
	ffstr s, m, name, val;
	char *fn = NULL;

	ffstr_setz(&s, meta);
	while (s.len != 0) {
		ffstr_shift(&s, ffstr_nextval(s.ptr, s.len, &m, ';'));

		if (ffstr_eqcz(&m, "clear")) {
			ent->no_tmeta = 1;
			continue;
		}

		if (NULL == ffs_split2by(m.ptr, m.len, '=', &name, &val)
			|| name.len == 0) {
			errlog(core, trk, "que", "--meta: invalid data");
			goto end;
		}

		f = FMED_QUE_OVWRITE;
		if (ffstr_matchz(&val, "@file:")) {
			ffstr_shift(&val, FFSLEN("@file:"));
			if (NULL == (fn = ffsz_alcopy(val.ptr, val.len)))
				goto end;
			if (0 != fffile_readall(&buf, fn, -1)) {
				syserrlog("%s: %s", fffile_read_S, fn);
				goto end;
			}
			ffstr_acqstr3(&val, &buf);
			f |= FMED_QUE_ACQUIRE;
		}

		_que_meta_set(&ent->e, name.ptr, name.len, val.ptr, val.len, f);
	}

	rc = 0;

end:
	ffarr_free(&buf);
	ffmem_free(fn);
	return rc;
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

static void que_play2(entry *ent, uint flags)
{
	fmed_que_entry *e = &ent->e;
	void *trk = qu->track->create(FMED_TRK_TYPE_PLAYBACK, e->url.ptr);
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

	if (qu->mixing)
		t->type = FMED_TRK_TYPE_MIXIN;

	if (e->dur != 0)
		qu->track->setval(trk, "track_duration", e->dur);
	if (e->from != 0)
		t->audio.abs_seek = e->from;
	if (e->to != 0 && FMED_NULL == t->audio.until)
		t->audio.until = e->to - e->from;

	ffstr *dict = ent->dict.ptr;
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

	FFARR2_FREE_ALL(&ent->tmeta, ffstr_free, ffstr);

	qu->track->setval(trk, "queue_item", (int64)e);
	ent_ref(ent);
	if (flags & 1)
		qu->track->cmd(trk, FMED_TRACK_XSTART);
	else
		qu->track->cmd(trk, FMED_TRACK_START);
}

/** Save playlist file. */
static void que_save(entry *first, const fflist_item *sentl, const char *fn)
{
	fffd f = FF_BADFD;
	ffm3u_cook m3 = {0};
	int rc = -1;
	entry *e;
	char buf[32];
	ffarr fname = {};

	if (0 == ffstr_catfmt(&fname, "%s.tmp%Z", fn))
		goto done;

	if (FF_BADFD == (f = fffile_open(fname.ptr, FFO_CREATE | FFO_TRUNC | FFO_WRONLY))) {
		if (0 != ffdir_make_path((void*)fname.ptr, 0))
			goto done;
		if (FF_BADFD == (f = fffile_open(fname.ptr, FFO_CREATE | FFO_TRUNC | FFO_WRONLY))) {
			syserrlog("%s: %s", fffile_open_S, fname.ptr);
			goto done;
		}
	}

	for (e = first;  &e->sib != sentl;  e = FF_GETPTR(entry, sib, e->sib.next)) {
		int r = 0;
		ffstr ss, *s;
		ss.len = 0;

		int dur = (e->e.dur != 0) ? e->e.dur / 1000 : -1;
		uint n = ffs_fromint(dur, buf, sizeof(buf), FFINT_SIGNED);
		r |= ffm3u_add(&m3, FFM3U_DUR, buf, n);

		if (NULL == (s = que_meta_find(&e->e, FFSTR("artist"))))
			s = &ss;
		r |= ffm3u_add(&m3, FFM3U_ARTIST, s->ptr, s->len);

		if (NULL == (s = que_meta_find(&e->e, FFSTR("title"))))
			s = &ss;
		r |= ffm3u_add(&m3, FFM3U_TITLE, s->ptr, s->len);

		r |= ffm3u_add(&m3, FFM3U_URL, e->e.url.ptr, e->e.url.len);

		if (r != 0)
			goto done;
	}

	if (m3.buf.len != (size_t)fffile_write(f, m3.buf.ptr, m3.buf.len))
		goto done;
	fffile_safeclose(f);
	if (0 != fffile_rename(fname.ptr, fn))
		goto done;
	dbglog(core, NULL, "que", "saved playlist to %s (%L KB)", fn, m3.buf.len / 1024);
	rc = 0;

done:
	if (rc != 0)
		syserrlog("saving playlist to file: %s", fn);
	ffarr_free(&fname);
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffm3u_fin(&m3);
}

static entry* que_getnext(entry *from)
{
	ffchain_item *it;
	plist *pl = (from == NULL) ? qu->curlist : from->plist;
	fflist *ents = &pl->ents;

	if (pl->allow_random && qu->random && pl->indexes.len != 0) {
		rnd_init();
		uint i = ffrnd_get() % pl->indexes.len;
		entry *e = ((entry**)pl->indexes.ptr) [i];
		return e;
	}

	it = (from == NULL) ? ents->first : from->sib.next;
	if (it == fflist_sentl(ents)) {
		if (qu->repeat_all)
			it = ents->first;

		if (it == fflist_sentl(ents)) {
			dbglog(core, NULL, "que", "no next file in playlist");
			qu->track->cmd(NULL, FMED_TRACK_LAST);
			return NULL;
		}

		dbglog(core, NULL, "que", "repeat_all: starting from the beginning");
	}

	return FF_GETPTR(entry, sib, it);
}

static void que_mix(void)
{
	fflist *ents = &qu->curlist->ents;
	void *mxout;
	entry *e;

	if (NULL == (mxout = qu->track->create(FMED_TRACK_MIX, NULL)))
		return;
	qu->track->setval(mxout, "mix_tracks", ents->len);
	ent_ref(mxout);
	qu->track->cmd(mxout, FMED_TRACK_START);

	qu->mixing = 1;
	FFLIST_WALK(ents, e, sib) {
		que_play(e);
	}
}

/** Get playlist by its index. */
static plist* plist_by_idx(size_t idx)
{
	uint i = 0;
	fflist_item *li;
	FFLIST_FOREACH(&qu->plists, li) {
		if (i++ == idx)
			return FF_GETPTR(plist, sib, li);
	}
	return NULL;
}

struct plist_sortdata {
	ffstr meta; // meta key by which to sort
	uint url :1
		, dur :1
		, reverse :1;
};

/** Compare two entries. */
static int plist_entcmp(const void *a, const void *b, void *udata)
{
	struct plist_sortdata *ps = udata;
	struct entry *e1 = *(void**)a, *e2 = *(void**)b;
	ffstr *s1, *s2;
	int r = 0;

	if (ps->url) {
		r = ffstr_cmp2(&e1->e.url, &e2->e.url);

	} else if (ps->dur) {
		r = ffint_cmp(e1->e.dur, e2->e.dur);

	} else if (ps->meta.len != 0) {
		s1 = que_meta_find(&e1->e, ps->meta.ptr, ps->meta.len);
		s2 = que_meta_find(&e2->e, ps->meta.ptr, ps->meta.len);
		if (s1 == NULL && s2 == NULL)
			r = 0;
		else if (s1 == NULL)
			r = 1;
		else if (s2 == NULL)
			r = -1;
		else
			r = ffstr_cmp2(s1, s2);
	}

	if (ps->reverse)
		r = -r;
	return r;
}

/** Initialize random number generator */
static void rnd_init()
{
	if (qu->rnd_ready)
		return;
	qu->rnd_ready = 1;
	fftime t;
	fftime_now(&t);
	ffrnd_seed(t.sec);
}

/** Sort indexes randomly */
static void sort_random(plist *pl)
{
	rnd_init();
	entry **arr = (void*)pl->indexes.ptr;
	for (size_t i = 0;  i != pl->indexes.len;  i++) {
		size_t to = ffrnd_get() % pl->indexes.len;
		_ffarr_swap(&arr[i], &arr[to], 1, sizeof(entry*));
	}
}

/** Sort playlist entries. */
static void plist_sort(struct plist *pl, const char *by, uint flags)
{
	if (ffsz_eq(by, "__random")) {
		sort_random(pl);
	} else {
		struct plist_sortdata ps = {};
		if (ffsz_eq(by, "__url"))
			ps.url = 1;
		else if (ffsz_eq(by, "__dur"))
			ps.dur = 1;
		else
			ffstr_setz(&ps.meta, by);
		ps.reverse = !!(flags & 1);
		ffsort(pl->indexes.ptr, pl->indexes.len, sizeof(void*), &plist_entcmp, &ps);
	}

	fflist_init(&pl->ents);
	entry **arr = (void*)pl->indexes.ptr;
	for (size_t i = 0;  i != pl->indexes.len;  i++) {
		fflist_ins(&pl->ents, &arr[i]->sib);
	}
}

static void que_cmd(uint cmd, void *param)
{
	que_cmd2(cmd, param, 0);
}

// matches enum FMED_QUE
static const char *const scmds[] = {
	"play", "play-excl", "mix", "stop-after", "next", "prev", "save", "clear", "add", "rm", "rmdead",
	"meta-set", "setonchange", "expand", "have-user-meta",
	"que-new", "que-del", "que-sel", "que-list", "is-curlist",
	"id", "item", "item-locked", "item-unlock",
	"flt-new", "flt-add", "flt-del", "lst-noflt",
	"sort", "count",
	"xplay", "add2", "add-after", "settrackprops", "copytrackprops",
	"", "", "", "",
};

static ssize_t que_cmdv(uint cmd, ...)
{
	ssize_t r = 0;
	struct plist *pl;
	uint cmdflags = cmd & _FMED_QUE_FMASK;
	cmd &= ~_FMED_QUE_FMASK;
	va_list va;
	va_start(va, cmd);

	switch (cmd) {
	case FMED_QUE_SORT:
	case FMED_QUE_COUNT:
		FF_ASSERT(_FMED_QUE_LAST == FFCNT(scmds));
		dbglog0("received command:%s", scmds[cmd]);
		break;
	}

	switch ((enum FMED_QUE)cmd) {
	case FMED_QUE_XPLAY: {
		fmed_que_entry *first = va_arg(va, fmed_que_entry*);
		entry *e = FF_GETPTR(entry, e, first);
		dbglog0("received command:%s  first-entry:%p", scmds[cmd], e);
		/* Note: we should reset it after the last track is stopped,
		 but this isn't required now - no one will call FMED_QUE_PLAY on this playlist.
		*/
		e->plist->parallel = 1;
		que_xplay(e);
		goto end;
	}

	case FMED_QUE_ADD2: {
		int plist_idx = va_arg(va, int);
		fmed_que_entry *ent = va_arg(va, fmed_que_entry*);

		plist *pl = qu->curlist;
		if (plist_idx >= 0)
			pl = plist_by_idx(plist_idx);

		r = (ssize_t)que_add(pl, ent, NULL, cmdflags);
		goto end;
	}

	case FMED_QUE_ADDAFTER: {
		fmed_que_entry *ent = va_arg(va, void*);
		fmed_que_entry *qprev = va_arg(va, void*);
		entry *prev = FF_GETPTR(entry, e, qprev);
		r = (ssize_t)que_add(prev->plist, ent, prev, cmdflags);
		goto end;
	}

	case FMED_QUE_COUNT: {
		pl = qu->curlist;
		r = pl->ents.len;
		goto end;
	}

	case FMED_QUE_DEL:
		pl = plist_by_idx(va_arg(va, int));
		fflist_rm(&qu->plists, &pl->sib);
		FFLIST_ENUMSAFE(&pl->ents, ent_rm, entry, sib);
		if (fflist_empty(&pl->ents))
			plist_free(pl);
		else
			pl->rm = 1;
		break;

	case FMED_QUE_SORT: {
		int plist_idx = va_arg(va, int);
		const char *by = va_arg(va, char*);
		int flags = va_arg(va, int);
		pl = qu->curlist;
		if (plist_idx >= 0)
			pl = plist_by_idx(plist_idx);
		plist_sort(pl, by, flags);
		goto end;
	}

	case FMED_QUE_ITEMLOCKED: {
		ssize_t plid = va_arg(va, size_t);
		ssize_t idx = va_arg(va, size_t);
		fflk_lock(&qu->plist_lock);
		r = que_cmdv(FMED_QUE_ITEM, plid, idx);
		if ((void*)r == NULL)
			fflk_unlock(&qu->plist_lock);
		goto end;
	}

	case FMED_QUE_ITEMUNLOCK: {
		// struct entry *e = va_arg(va, void*);
		fflk_unlock(&qu->plist_lock);
		goto end;
	}

	case FMED_QUE_SETTRACKPROPS: {
		fmed_que_entry *qent = va_arg(va, void*);
		fmed_trk *trk = va_arg(va, void*);
		entry *e = FF_GETPTR(entry, e, qent);
		if (e->trk == NULL && NULL == (e->trk = ffmem_allocT(1, fmed_trk))) {
			r = -1;
			goto end;
		}
		qu->track->copy_info(e->trk, trk);
		goto end;
	}

	case FMED_QUE_COPYTRACKPROPS: {
		fmed_que_entry *qent = va_arg(va, void*);
		fmed_que_entry *qsrc = va_arg(va, void*);
		entry *e = FF_GETPTR(entry, e, qent);
		entry *src = FF_GETPTR(entry, e, qsrc);

		if (src->trk != NULL)
			que_cmdv(FMED_QUE_SETTRACKPROPS, qent, src->trk);

		const ffstr *dict = src->dict.ptr;
		for (uint i = 0;  i != src->dict.len;  i += 2) {
			if ((ssize_t)dict[i + 1].len >= 0)
				que_meta_set(&e->e, &dict[i], &dict[i + 1], FMED_QUE_TRKDICT);
			else {
				ffstr s;
				ffstr_set(&s, dict[i + 1].ptr, sizeof(int64));
				que_meta_set(&e->e, &dict[i], &s, FMED_QUE_TRKDICT | FMED_QUE_NUM);
			}
		}

		goto end;
	}

	case FMED_QUE_SET_RANDOM: {
		uint val = va_arg(va, uint);
		qu->random = val;
		goto end;
	}
	case FMED_QUE_SET_NEXTIFERROR: {
		uint val = va_arg(va, uint);
		qu->next_if_err = val;
		goto end;
	}
	case FMED_QUE_SET_REPEATALL: {
		uint val = va_arg(va, uint);
		qu->repeat_all = val;
		goto end;
	}
	case FMED_QUE_SET_QUITIFDONE: {
		uint val = va_arg(va, uint);
		qu->quit_if_done = val;
		goto end;
	}

	default:
		break;
	}

	void *param = va_arg(va, void*);
	size_t param2 = va_arg(va, size_t);
	r = que_cmd2(cmd, param, param2);

end:
	va_end(va);
	return r;
}

static ssize_t que_cmd2(uint cmd, void *param, size_t param2)
{
	plist *pl;
	fflist *ents = &qu->curlist->ents;
	entry *e;
	uint flags = cmd & _FMED_QUE_FMASK;
	cmd &= ~_FMED_QUE_FMASK;

	FF_ASSERT(_FMED_QUE_LAST == FFCNT(scmds));
	if (cmd != FMED_QUE_ITEM)
		dbglog0("received command:%s, param:%p", scmds[cmd], param);

	switch ((enum FMED_QUE)cmd) {
	case FMED_QUE_PLAY_EXCL:
		qu->track->cmd(NULL, FMED_TRACK_STOPALL);
		// break

	case FMED_QUE_PLAY:
		pl = qu->curlist;
		if (param != NULL) {
			e = param;
			pl = e->plist;
			pl->cur = param;

		} else if (pl->cur == NULL) {
			if (NULL == (pl->cur = que_getnext(NULL)))
				break;
		}
		qu->mixing = 0;
		que_play(pl->cur);
		break;

	case FMED_QUE_MIX:
		que_mix();
		break;

	case FMED_QUE_STOP_AFTER:
		pl = qu->curlist;
		if (pl->cur != NULL && pl->cur->refcount != 0)
			pl->cur->stop_after = 1;
		break;

	case FMED_QUE_NEXT2:
		pl = qu->curlist;
		e = pl->cur;
		if (param != NULL) {
			e = FF_GETPTR(entry, e, param);
			pl = e->plist;
		}
		if (NULL != (e = que_getnext(e))) {
			pl->cur = e;
			que_play(pl->cur);
		}
		break;

	case FMED_QUE_PREV2:
		pl = qu->curlist;
		e = pl->cur;
		if (param != NULL) {
			e = FF_GETPTR(entry, e, param);
			pl = e->plist;
		}
		if (pl->cur == NULL || pl->cur->sib.prev == fflist_sentl(&pl->cur->plist->ents)) {
			pl->cur = NULL;
			dbglog(core, NULL, "que", "no previous file in playlist");
			break;
		}
		pl->cur = FF_GETPTR(entry, sib, pl->cur->sib.prev);
		que_play(pl->cur);
		break;

	case FMED_QUE_ADD:
		return (ssize_t)que_add(qu->curlist, param, NULL, flags);

	case FMED_QUE_EXPAND: {
		void *r = param;
		e = FF_GETPTR(entry, e, r);
		void *trk = qu->track->create(FMED_TRK_TYPE_EXPAND, e->e.url.ptr);
		fmed_trk *t = qu->track->conf(trk);
		t->input_info = 1;
		e->expand = 1;
		qu->track->setval(trk, "queue_item", (int64)e);
		ent_ref(e);
		qu->track->cmd(trk, FMED_TRACK_START);
		return (size_t)r;
	}

	case FMED_QUE_RM:
		e = param;
		dbglog(core, NULL, "que", "removed item %S", &e->e.url);

		if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL)
			qu->onchange(&e->e, FMED_QUE_ONRM);

		fflk_lock(&qu->plist_lock);
		ent_rm(e);
		fflk_unlock(&qu->plist_lock);
		break;

	case FMED_QUE_RMDEAD: {
		fflist_item *it;
		FFLIST_FOR(ents, it) {
			e = FF_GETPTR(entry, sib, it);
			it = it->next;
			if (!fffile_exists(e->e.url.ptr)) {
				que_cmd(FMED_QUE_RM, e);
			}
		}
		}
		break;

	case FMED_QUE_METASET:
		{
		const ffstr *pair = (void*)param2;
		que_meta_set(param, &pair[0], &pair[1], flags >> 16);
		}
		break;

	case FMED_QUE_HAVEUSERMETA:
		e = param;
		return (e->meta.len != 0 || e->no_tmeta);


	case FMED_QUE_SAVE:
		if ((ssize_t)param == -1)
			pl = qu->curlist;
		else
			pl = plist_by_idx((size_t)param);
		if (pl == NULL)
			return -1;
		ents = &pl->ents;
		que_save(FF_GETPTR(entry, sib, ents->first), fflist_sentl(ents), (void*)param2);
		break;

	case FMED_QUE_CLEAR:
		fflk_lock(&qu->plist_lock);
		FFLIST_ENUMSAFE(ents, ent_rm, entry, sib);
		fflk_unlock(&qu->plist_lock);
		dbglog(core, NULL, "que", "cleared");
		if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL)
			qu->onchange(NULL, FMED_QUE_ONCLEAR);
		break;

	case FMED_QUE_SETONCHANGE:
		qu->onchange = param;
		break;


	case FMED_QUE_NEW: {
		uint f = (size_t)param;
		if (NULL == (pl = ffmem_tcalloc1(plist)))
			return -1;
		pl->allow_random = !(f & FMED_QUE_NORND);
		fflist_init(&pl->ents);
		fflist_ins(&qu->plists, &pl->sib);
		break;
	}

	case FMED_QUE_SEL:
		{
		uint i = 0, n = (uint)(size_t)param;
		fflist_item *li;

		if (n >= qu->plists.len)
			return -1;

		FFLIST_FOREACH(&qu->plists, li) {
			if (i++ == n)
				break;
		}
		qu->curlist = FF_GETPTR(plist, sib, li);
		}
		break;

	case FMED_QUE_LIST:
		if (qu->curlist->filtered_plist != NULL) {
			fmed_que_entry **ent = param;
			pl = qu->curlist->filtered_plist;
			uint i = 0;
			if (*ent != NULL)
				i = que_cmdv(FMED_QUE_ID, *ent) + 1;
			if (i == pl->indexes.len)
				return 0;
			*ent = (void*)que_cmdv(FMED_QUE_ITEM, (size_t)i);
			return 1;
		}
		//fallthrough

	case FMED_QUE_LIST_NOFILTER:
		{
		fmed_que_entry **ent = param;
		ffchain_item *it;
		if (*ent == NULL) {
			it = ents->first;
		} else {
			e = FF_GETPTR(entry, e, *ent);
			it = e->sib.next;
		}
		for (;;) {
			if (it == fflist_sentl(ents))
				return 0;
			e = FF_GETPTR(entry, sib, it);
			if (!e->rm)
				break;
			it = e->sib.next;
		}
		*ent = &e->e;
		}
		return 1;

	case FMED_QUE_ISCURLIST:
		e = param;
		return (e->plist == qu->curlist);

	case FMED_QUE_ID:
		e = param;
		pl = e->plist;
		if (pl->filtered_plist != NULL)
			pl = pl->filtered_plist;
		return plist_ent_idx(pl, e);

	case FMED_QUE_ITEM: {
		ssize_t plid = (size_t)param;
		pl = (plid != -1) ? plist_by_idx(plid) : qu->curlist;
		if (pl->filtered_plist != NULL)
			pl = pl->filtered_plist;
		return (size_t)plist_ent(pl, param2);
	}

	case FMED_QUE_NEW_FILTERED:
		if (qu->curlist->filtered_plist != NULL)
			que_cmdv(FMED_QUE_DEL_FILTERED);
		if (NULL == (pl = ffmem_new(struct plist)))
			return -1;
		fflist_init(&pl->ents);
		pl->filtered = 1;
		qu->curlist->filtered_plist = pl;
		break;

	case FMED_QUE_ADD_FILTERED:
		e = param;
		pl = qu->curlist->filtered_plist;
		if (NULL == ffarr_growT(&pl->indexes, 1, 16, entry*))
			return -1;
		*ffarr_pushT(&pl->indexes, entry*) = e;
		break;

	case FMED_QUE_DEL_FILTERED:
		plist_free(qu->curlist->filtered_plist);
		qu->curlist->filtered_plist = NULL;
		break;

	case _FMED_QUE_LAST:
		break;

	default:
		break;
	}

	return 0;
}

static fmed_que_entry* _que_add(fmed_que_entry *ent)
{
	return (void*)que_cmd2(FMED_QUE_ADD, ent, 0);
}

static fmed_que_entry* que_add(plist *pl, fmed_que_entry *ent, entry *prev, uint flags)
{
	entry *e = NULL;

	if (flags & FMED_QUE_ADD_DONE) {
		if (ent == NULL) {
			if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL)
				qu->onchange(&e->e, FMED_QUE_ONADD | FMED_QUE_ADD_DONE);
			return NULL;
		}
		e = FF_GETPTR(entry, e, ent);
		goto done;
	}

	e = ffmem_alloc(sizeof(entry) + ent->url.len + 1);
	if (e == NULL)
		return NULL;
	ffmem_tzero(e);
	e->plist = pl;

	ffsz_copy(e->url, ent->url.len + 1, ent->url.ptr, ent->url.len);
	e->e.url.ptr = e->url;
	e->e.url.len = ent->url.len;

	e->e.from = ent->from;
	e->e.to = ent->to;
	e->e.dur = ent->dur;

	fflk_lock(&qu->plist_lock);

	ffchain_append(&e->sib, (prev != NULL) ? &prev->sib : e->plist->ents.last);
	e->plist->ents.len++;
	if (NULL == ffarr_growT(&e->plist->indexes, 1, FFARR_GROWQUARTER | 16, entry*)) {
		ent_rm(e);
		fflk_unlock(&qu->plist_lock);
		return NULL;
	}
	ssize_t i = e->plist->indexes.len;
	if (prev != NULL) {
		i = plist_ent_idx(e->plist, prev);
		FF_ASSERT(i != -1);
		i++;
		_ffarr_shiftr(&e->plist->indexes, i, 1, sizeof(entry*));
	}
	e->list_pos = i;
	((entry**)e->plist->indexes.ptr) [i] = e;
	e->plist->indexes.len++;
	fflk_unlock(&qu->plist_lock);

	dbglog(core, NULL, "que", "added: (%d: %d-%d) %S"
		, ent->dur, ent->from, ent->to, &ent->url);

done:
	if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL)
		qu->onchange(&e->e, FMED_QUE_ONADD | (flags & FMED_QUE_MORE));
	return &e->e;
}

static int que_arrfind(const ffstr *m, uint n, const char *name, size_t name_len)
{
	uint i;
	for (i = 0;  i != n;  i += 2) {
		if (ffstr_ieq(&m[i], name, name_len))
			return i;
	}
	return -1;
}

static void _que_meta_set(fmed_que_entry *ent, const char *name, size_t name_len, const char *val, size_t val_len, uint flags)
{
	ffstr pair[2];
	ffstr_set(&pair[0], name, name_len);
	ffstr_set(&pair[1], val, val_len);
	que_cmd2(FMED_QUE_METASET | (flags << 16), ent, (size_t)pair);
}

static void que_meta_set(fmed_que_entry *ent, const ffstr *name, const ffstr *val, uint flags)
{
	entry *e = FF_GETPTR(entry, e, ent);
	char *sname, *sval;
	ffarr2 *a;

	if (!(flags & FMED_QUE_NUM)) {
		dbglog0("meta #%u: %S: %S f:%xu"
			, (e->meta.len + e->tmeta.len) / 2 + 1, name, val, flags);
	}

	a = &e->meta;
	if (flags & FMED_QUE_TMETA) {
		if (e->no_tmeta)
			return;
		a = &e->tmeta;

	} else if (flags & FMED_QUE_TRKDICT) {
		a = &e->dict;
		if ((flags & FMED_QUE_NUM) && val->len != sizeof(int64))
			return;
	}

	if (!(flags & FMED_QUE_PRIV) && ffstr_matchz(name, "__")) {
		fmed_warnlog(core, NULL, "queue", "meta names starting with \"__\" are considered private: \"%S\""
			, name);
		return;
	}

	if (flags & (FMED_QUE_OVWRITE | FMED_QUE_METADEL)) {
		int i = que_arrfind(a->ptr, a->len, name->ptr, name->len);

		if (i == -1) {

		} else if (flags & FMED_QUE_METADEL) {
			fflk_lock(&qu->plist_lock);
			ffarr ar;
			ffarr_set3(&ar, (void*)a->ptr, a->len, a->len);
			_ffarr_rm(&ar, i, 2, sizeof(ffstr));
			a->len -= 2;
			fflk_unlock(&qu->plist_lock);

		} else {
			if (NULL == (sval = ffsz_alcopy(val->ptr, val->len)))
				goto err;

			fflk_lock(&qu->plist_lock);
			ffstr *arr = a->ptr;
			ffstr_free(&arr[i + 1]);
			ffstr_set(&arr[i + 1], sval, val->len);
			fflk_unlock(&qu->plist_lock);
		}

		if (a == &e->meta) {
			ffstr empty;
			empty.len = 0;
			que_meta_set(ent, name, &empty, FMED_QUE_TMETA | FMED_QUE_METADEL | (flags & ~FMED_QUE_OVWRITE));
		}

		if (i != -1)
			return;

		if (flags & FMED_QUE_METADEL)
			return;
	}

	if (NULL == (sname = ffsz_alcopylwr(name->ptr, name->len)))
		goto err;
	if (flags & FMED_QUE_ACQUIRE)
		sval = val->ptr;
	else {
		if (NULL == (sval = ffsz_alcopy(val->ptr, val->len)))
			goto err;
	}

	fflk_lock(&qu->plist_lock);
	if (NULL == ffarr2_grow(a, 2, sizeof(ffstr))) {
		fflk_unlock(&qu->plist_lock);
		goto err;
	}

	ffstr *arr = a->ptr;
	ffstr_set(&arr[a->len], sname, name->len);
	ffstr_set(&arr[a->len + 1], sval, val->len);
	if ((flags & (FMED_QUE_TRKDICT | FMED_QUE_NUM)) == (FMED_QUE_TRKDICT | FMED_QUE_NUM))
		arr[a->len + 1].len = -(ssize_t)arr[a->len + 1].len;
	a->len += 2;
	fflk_unlock(&qu->plist_lock);
	return;

err:
	if (flags & FMED_QUE_ACQUIRE)
		ffmem_free(val->ptr);
	syserrlog("%s", ffmem_alloc_S);
}

static ffstr* que_meta_find(fmed_que_entry *ent, const char *name, size_t name_len)
{
	int i;
	entry *e = FF_GETPTR(entry, e, ent);

	if (name_len == (size_t)-1)
		name_len = ffsz_len(name);

	for (uint k = 0;  k != 2;  k++) {
		const ffarr2 *meta = (k == 0) ? &e->meta : &e->tmeta;
		if (-1 != (i = que_arrfind(meta->ptr, meta->len, name, name_len)))
			return &((ffstr*)meta->ptr)[i + 1];
	}

	return NULL;
}

static ffstr* que_meta(fmed_que_entry *ent, size_t n, ffstr *name, uint flags)
{
	entry *e = FF_GETPTR(entry, e, ent);
	size_t nn;
	ffstr *m;

	n *= 2;
	if (n == e->meta.len + e->tmeta.len)
		return NULL;

	if (n < e->meta.len) {
		m = e->meta.ptr;
		nn = n;
	} else {
		if (flags & FMED_QUE_NO_TMETA)
			return NULL;
		m = e->tmeta.ptr;
		nn = n - e->meta.len;
	}

	*name = m[nn];

	if (flags & FMED_QUE_UNIQ) {
		if (-1 != que_arrfind(e->meta.ptr, ffmin(n, e->meta.len), name->ptr, name->len))
			return FMED_QUE_SKIP;

		if (n >= e->meta.len) {
			if (-1 != que_arrfind(e->tmeta.ptr, nn, name->ptr, name->len))
				return FMED_QUE_SKIP;
		}
	}

	if (ffstr_matchz(name, "__"))
		return FMED_QUE_SKIP;

	return &m[nn + 1];
}


static void que_ontrkfin(entry *e)
{
	if (qu->mixing) {
		if (qu->quit_if_done && e->trk_mixed)
			core->sig(FMED_STOP);
	} else if (e->stop_after)
		e->stop_after = 0;
	else if (e->expand || e->trk_stopped)
	{}
	else if (!e->trk_err)
		que_cmd(FMED_QUE_NEXT2, &e->e);
	ent_unref(e);
}

static void que_taskfunc(void *udata)
{
	struct quetask *qt = udata;
	qt->tsk.handler = NULL;
	switch ((enum CMD)qt->cmd) {
	case CMD_TRKFIN:
		que_ontrkfin((void*)qt->param);
		break;
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

	if ((int64)t->d->audio.total != FMED_NULL && t->d->audio.fmt.sample_rate != 0)
		t->e->e.dur = ffpcm_time(t->d->audio.total, t->d->audio.fmt.sample_rate);

	int stopped = t->track->getval(t->trk, "stopped");
	int err = t->track->getval(t->trk, "error");
	t->e->trk_stopped = (stopped != FMED_NULL);
	t->e->trk_err = (err != FMED_NULL)
		&& !qu->next_if_err;
	t->e->trk_mixed = (FMED_NULL != t->track->getval(t->trk, "mix_tracks"));

	struct quetask *qt = ffmem_new(struct quetask);
	FF_ASSERT(qt != NULL);
	qt->cmd = CMD_TRKFIN;
	qt->param = (size_t)t->e;
	que_task_add(qt);

	ffmem_free(t);
}

static int que_trk_process(void *ctx, fmed_filt *d)
{
	d->outlen = 0;
	return FMED_RDONE;
}
