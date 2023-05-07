/** Track queue.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <FFOS/dir.h>
#include <FFOS/random.h>
#include <avpack/m3u.h>


#undef syserrlog
#define infolog(...)  fmed_infolog(core, NULL, "queue", __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, "queue", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "queue", __VA_ARGS__)

#define MAX_N_ERRORS  15  // max. number of consecutive errors

/*
Metadata priority:
  . from user (--meta)
    if "--meta=clear" is used, skip transient meta
  . from .cue
  . from file or ICY server (transient)
  . artist/title from .m3u (used as transient due to lower priority)
*/

static const fmed_core *core;

struct entry;
typedef struct entry entry;
typedef struct plist plist;

struct plist {
	fflist_item sib;
	fflist ents; //entry[]
	ffarr indexes; //entry*[]  Get an entry by its number;  find a number by an entry pointer.
	entry *cur, *xcursor;
	struct plist *filtered_plist; //list with the filtered tracks
	uint nerrors; // number of consecutive errors
	uint rm :1;
	uint allow_random :1;
	uint filtered :1;
	uint parallel :1; // every item in this queue will start via FMED_TRACK_XSTART
	uint expand_all :1; // read meta data of all items
};

static void plist_free(plist *pl);
static ssize_t plist_ent_idx(plist *pl, entry *e);
static struct entry* plist_ent(struct plist *pl, size_t idx);
static entry* pl_first(plist *pl);

struct que_conf {
	byte next_if_err;
	byte rm_nosrc;
	byte rm_unkifmt;
};

typedef struct que {
	fflist plists; //plist[]
	plist *curlist;
	plist *pl_playing;
	const fmed_track *track;
	fmed_que_onchange_t onchange;
	fflock plist_lock;

	struct que_conf conf;
	uint list_random;
	uint repeat;
	uint quit_if_done :1
		, next_if_err :1
		, fmeta_lowprio :1 //meta from file has lower priority
		, rnd_ready :1
		, random :1
		, random_split :1 // random: logically split playlist into 2 chunks for better UXP
		, mixing :1;
} que;

static que *qu;


//FMEDIA MODULE
static const void* que_iface(const char *name);
static int que_mod_conf(const char *name, fmed_conf_ctx *ctx);
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
void que_meta_set2(fmed_que_entry *ent, ffstr name, ffstr val, uint flags);
static ffstr* que_meta_find(fmed_que_entry *ent, const char *name, size_t name_len);
static ffstr* que_meta(fmed_que_entry *ent, size_t n, ffstr *name, uint flags);
const fmed_queue fmed_que_mgr = {
	&que_cmdv, &que_cmd, &que_cmd2, &_que_add, &_que_meta_set, &que_meta_find, &que_meta, que_meta_set2
};

static fmed_que_entry* que_add(plist *pl, fmed_que_entry *ent, entry *prev, uint flags);
static void que_play(entry *e);
static void que_play2(entry *ent, uint flags);
static void ent_start_prepare(entry *e, void *trk);
static void rnd_init();
static void plist_remove_entry(entry *e, ffbool from_index, ffbool remove);
static entry* que_getnext(entry *from);
static void pl_expand_next(plist *pl, entry *e);

#include <core/queue-entry.h>
#include <core/queue-track.h>

static const fmed_conf_arg que_conf_args[] = {
	{ "next_if_error",	FMC_BOOL8,  FMC_O(struct que_conf, next_if_err) },
	{ "remove_if_no_source",	FMC_BOOL8,  FMC_O(struct que_conf, rm_nosrc) },
	{ "remove_if_unknown_format",	FMC_BOOL8,  FMC_O(struct que_conf, rm_unkifmt) },
	{}
};
static int que_config(fmed_conf_ctx *ctx)
{
	qu->conf.next_if_err = 1;
	qu->conf.rm_unkifmt = 1;
	fmed_conf_addctx(ctx, &qu->conf, que_conf_args);
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

static int que_mod_conf(const char *name, fmed_conf_ctx *ctx)
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

/** Prepare the item before starting a track. */
static void ent_start_prepare(entry *e, void *trk)
{
	FFSLICE_FOREACH_T(&e->tmeta, ffstr_free, ffstr);
	ffslice_free(&e->tmeta);
	qu->track->setval(trk, "queue_item", (int64)e);
	ent_ref(e);
}

static void plist_remove_entry(entry *e, ffbool from_index, ffbool remove)
{
	plist *pl = e->plist;

	if (from_index) {
		ssize_t i = plist_ent_idx(pl, e);
		if (i >= 0)
			ffslice_rmT((ffslice*)&pl->indexes, i, 1, entry*);

		if (pl->filtered_plist != NULL) {
			i = plist_ent_idx(pl->filtered_plist, e);
			if (i >= 0)
				ffslice_rmT((ffslice*)&pl->filtered_plist->indexes, i, 1, entry*);
		}
	}

	if (!remove)
		return;

	if (pl->cur == e)
		pl->cur = NULL;
	if (pl->xcursor == e)
		pl->xcursor = NULL;
	fflist_rm(&pl->ents, &e->sib);
	if (pl->ents.len == 0 && pl->rm)
		plist_free(pl);
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


/** Simple file anti-overwrite filter.
Store all input data in memory; write to disk only if the data is changed */
struct plaow {
	ffvec buf;
};
static void* plaow_open(fmed_track_info *ti)
{
	return ffmem_new(struct plaow);
}
static void plaow_close(void *ctx)
{
	struct plaow *p = ctx;
	ffvec_free(&p->buf);
	ffmem_free(p);
}
static int plaow_process(void *ctx, fmed_track_info *ti)
{
	struct plaow *p = ctx;
	int rc;

	if (ti->flags & FMED_FFWD)
		ffvec_addstr(&p->buf, &ti->data_in);

	if (!(ti->flags & FMED_FFIRST))
		return FMED_RMORE;

	ffvec d = {};
	fffile_readwhole(ti->out_filename, &d, 10*1024*1024);

	if (ffstr_eq2(&p->buf, &d)) {
		rc = FMED_RFIN; // the playlist hasn't changed
		goto end;
	}

	ffstr_setstr(&ti->data_out, &p->buf);
	rc = FMED_RDONE;

end:
	ffvec_free(&d);
	return rc;
}
static const fmed_filter fmed_plaow = { plaow_open, plaow_process, plaow_close };


/** Save playlist file. */
static fmed_track_obj* que_export(plist *pl, const char *fn)
{
	fmed_track_obj *t = qu->track->create(FMED_TRK_TYPE_PLIST, NULL);
	fmed_track_info *ti = qu->track->conf(t);

	uint add_cmd = FMED_TRACK_FILT_ADDLAST;
	const char *file_write_filter = "#file.out";

	entry *first = pl_first(pl);
	ti->que_cur = &first->e;
	qu->track->cmd(t, add_cmd, "plist.m3u-out");

	ffstr ext;
	ffpath_split3_str(FFSTR_Z(fn), NULL, NULL, &ext);
	if (ffstr_eqz(&ext, "m3uz"))
		qu->track->cmd(t, add_cmd, "zstd.compress");

	qu->track->cmd(t, FMED_TRACK_FILT_ADDF, add_cmd, "pl-aow", &fmed_plaow);

	ti->out_filename = ffsz_dup(fn);
	ti->out_overwrite = 1;
	ti->out_name_tmp = 1;
	qu->track->cmd(t, add_cmd, file_write_filter);

	qu->track->cmd(t, FMED_TRACK_START);
	return t;
}

/** Get the first playlist item. */
static entry* pl_first(plist *pl)
{
	if (fflist_first(&pl->ents) == fflist_sentl(&pl->ents))
		return NULL;
	return FF_GETPTR(entry, sib, fflist_first(&pl->ents));
}

/** Get the next item.
from: previous item
Return NULL if there's no next item */
static entry* pl_next(entry *from)
{
	ffchain_item *it = from->sib.next;
	if (it == fflist_sentl(&from->plist->ents))
		return NULL;
	return FF_GETPTR(entry, sib, it);
}

/** Get previous playlist entry */
static entry* pl_prev(plist *pl, entry *e)
{
	if (e == NULL || e->sib.prev == fflist_sentl(&pl->ents))
		return NULL;
	return FF_GETPTR(entry, sib, e->sib.prev);
}

/** Get random playlist index */
static ffsize pl_random(plist *pl)
{
	ffsize n = pl->indexes.len;
	if (n == 1)
		return 0;
	rnd_init();
	ffsize i = ffrnd_get();
	if (!qu->random_split)
		i %= n / 2;
	else
		i = n / 2 + (i % (n - (n / 2)));
	qu->random_split = !qu->random_split;
	dbglog0("rnd: %L", i);
	return i;
}

/** Get default playlist: the currently playing or currently shown */
static plist* pl_default()
{
	return (qu->pl_playing != NULL) ? qu->pl_playing : qu->curlist;
}

/** Get the next (or the first) item; apply "random" and "repeat" settings.
from: previous item
 NULL: get the first item
Return NULL if there's no next item */
static entry* que_getnext(entry *from)
{
	plist *pl = pl_default();
	if (from != NULL)
		pl = from->plist;

	if (pl->allow_random && qu->random && pl->indexes.len != 0) {
		ffsize i = pl_random(pl);
		entry *e = ((entry**)pl->indexes.ptr) [i];
		return e;
	}

	if (from == NULL) {
		from = pl_first(pl);
	} else if (qu->repeat == FMED_QUE_REPEAT_TRACK) {
		dbglog0("repeat: same track");
	} else {
		from = pl_next(from);
		if (from == NULL && qu->repeat == FMED_QUE_REPEAT_ALL) {
			dbglog0("repeat: starting from the beginning");
			from = pl_first(pl);
		}
	}
	if (from == NULL) {
		dbglog0("no next file in playlist");
		qu->track->cmd(NULL, FMED_TRACK_LAST);
	}
	return from;
}

/** Expand the next (or the first) item in list.
e: the last expanded item
 NULL: expand the first item */
static void pl_expand_next(plist *pl, entry *e)
{
	void *trk = NULL;

	if (e == NULL) {
		e = pl_first(pl);
		if (e == NULL)
			return;
		pl->expand_all = 1;
		dbglog0("expanding plist %p", pl);
		trk = (void*)que_cmdv(FMED_QUE_EXPAND, e);
	}

	while (trk == NULL || trk == FMED_TRK_EFMT) {
		e = pl_next(e);
		if (e == NULL) {
			pl->expand_all = 0;
			dbglog0("done expanding plist %p", pl);
			break;
		}

		trk = (void*)que_cmdv(FMED_QUE_EXPAND, e);
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

static plist* plist_by_useridx(int idx)
{
	plist *pl = qu->curlist;
	if (idx >= 0)
		pl = plist_by_idx(idx);
	return pl;
}

struct plist_sortdata {
	ffstr meta; // meta key by which to sort
	uint url :1
		, dur :1
		, reverse :1;
};

#define ffint_cmp(a, b) \
	(((a) == (b)) ? 0 : ((a) < (b)) ? -1 : 1)

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
		*arr[i] = FF_SWAP(arr[i], *arr[to]);
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

/** Get next item */
static int pl_list_next(fmed_que_entry **ent)
{
	ffchain_item *it;
	fflist *ents;
	entry *e;
	if (*ent == NULL) {
		ents = &qu->curlist->ents;
		it = fflist_first(ents);
	} else {
		e = FF_STRUCTPTR(entry, e, *ent);
		ents = &e->plist->ents;
		it = e->sib.next;
	}

	for (;;) {
		if (it == fflist_sentl(ents))
			return 0;
		e = FF_STRUCTPTR(entry, sib, it);
		if (!e->rm)
			break;
		it = e->sib.next;
	}

	*ent = &e->e;
	return 1;
}

static void que_cmd(uint cmd, void *param)
{
	que_cmd2(cmd, param, 0);
}

// matches enum FMED_QUE
static const char *const scmds[] = {
	"FMED_QUE_PLAY",
	"FMED_QUE_PLAY_EXCL",
	"FMED_QUE_MIX",
	"FMED_QUE_STOP_AFTER",
	"FMED_QUE_NEXT2",
	"FMED_QUE_PREV2",
	"FMED_QUE_SAVE",
	"FMED_QUE_CLEAR",
	"FMED_QUE_ADD",
	"FMED_QUE_RM",
	"FMED_QUE_RMDEAD",
	"FMED_QUE_METASET",
	"FMED_QUE_SETONCHANGE",
	"FMED_QUE_EXPAND",
	"FMED_QUE_HAVEUSERMETA",
	"FMED_QUE_NEW",
	"FMED_QUE_DEL",
	"FMED_QUE_SEL",
	"FMED_QUE_LIST",
	"FMED_QUE_ISCURLIST",
	"FMED_QUE_ID",
	"FMED_QUE_ITEM",
	"FMED_QUE_ITEMLOCKED",
	"FMED_QUE_ITEMUNLOCK",
	"FMED_QUE_NEW_FILTERED",
	"FMED_QUE_ADD_FILTERED",
	"FMED_QUE_DEL_FILTERED",
	"FMED_QUE_LIST_NOFILTER",
	"FMED_QUE_SORT",
	"FMED_QUE_COUNT",
	"FMED_QUE_COUNT2",
	"FMED_QUE_XPLAY",
	"FMED_QUE_ADD2",
	"FMED_QUE_ADDAFTER",
	"FMED_QUE_SETTRACKPROPS",
	"FMED_QUE_COPYTRACKPROPS",
	"FMED_QUE_SET_RANDOM",
	"FMED_QUE_SET_NEXTIFERROR",
	"FMED_QUE_SET_REPEATALL",
	"FMED_QUE_SET_QUITIFDONE",
	"FMED_QUE_EXPAND2",
	"FMED_QUE_EXPAND_ALL",
	"FMED_QUE_CURID",
	"FMED_QUE_SETCURID",
	"FMED_QUE_N_LISTS",
	"FMED_QUE_FLIP_RANDOM",
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
		plist *pl = plist_by_useridx(va_arg(va, int));
		fmed_que_entry *ent = va_arg(va, fmed_que_entry*);
		if (pl == NULL) {
			r = 0;
			goto end;
		}
		r = (ssize_t)que_add(pl, ent, NULL, cmdflags);
		goto end;
	}

	case FMED_QUE_ADDAFTER: {
		fmed_que_entry *ent = va_arg(va, void*);
		fmed_que_entry *qprev = va_arg(va, void*);
		entry *prev = NULL;
		plist *pl = qu->curlist;
		if (qprev != NULL) {
			prev = FF_GETPTR(entry, e, qprev);
			pl = prev->plist;
		}
		r = (ssize_t)que_add(pl, ent, prev, cmdflags);
		goto end;
	}

	case FMED_QUE_COUNT: {
		pl = qu->curlist;
		if (pl == NULL) {
			r = 0;
			goto end;
		}
		if (pl->filtered_plist != NULL)
			pl = pl->filtered_plist;
		r = pl->indexes.len;
		goto end;
	}

	case FMED_QUE_COUNT2: {
		pl = plist_by_useridx(va_arg(va, int));
		if (pl == NULL) {
			r = 0;
			goto end;
		}
		if (pl->filtered_plist != NULL)
			pl = pl->filtered_plist;
		r = pl->indexes.len;
		goto end;
	}

	case FMED_QUE_CURID: {
		int plid = va_arg(va, int);
		pl = plist_by_useridx(plid);
		if (pl == NULL)
			goto end;
		r = que_cmdv(FMED_QUE_ID, pl->cur);
		if (r == -1)
			r = 0;
		goto end;
	}

	case FMED_QUE_N_LISTS: {
		r = qu->plists.len;
		goto end;
	}

	case FMED_QUE_SETCURID: {
		int plid = va_arg(va, int);
		size_t eid = va_arg(va, size_t);
		pl = plist_by_useridx(plid);
		if (pl == NULL)
			goto end;
		pl->cur = (void*)que_cmdv(FMED_QUE_ITEM, plid, (size_t)eid);
		goto end;
	}

	case FMED_QUE_DEL:
		pl = plist_by_useridx(va_arg(va, int));
		if (pl == NULL)
			break;
		fflist_rm(&qu->plists, &pl->sib);
		FFLIST_ENUMSAFE(&pl->ents, ent_rm, entry, sib);
		if (fflist_empty(&pl->ents)) {
			plist_free(pl);
			if (qu->curlist == pl)
				qu->curlist = NULL;
			if (qu->pl_playing == pl)
				qu->pl_playing = NULL;
		} else
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
		int plid = va_arg(va, int);
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
		if (e->trk == NULL) {
			e->trk = ffmem_alloc(sizeof(fmed_trk));
			qu->track->copy_info(e->trk, NULL);
		}
		qu->track->copy_info(e->trk, trk);
		goto end;
	}

	case FMED_QUE_COPYTRACKPROPS: {
		fmed_que_entry *qent = va_arg(va, void*);
		fmed_que_entry *qsrc = va_arg(va, void*);
		if (qent == NULL || qsrc == NULL)
			goto end;
		entry *e = FF_GETPTR(entry, e, qent);
		entry *src = FF_GETPTR(entry, e, qsrc);
		que_copytrackprops(e, src);
		goto end;
	}

	case FMED_QUE_SET_RANDOM: {
		uint val = va_arg(va, uint);
		qu->random = val;
		goto end;
	}

	case FMED_QUE_FLIP_RANDOM:
		qu->random = !qu->random;
		r = qu->random;
		goto end;

	case FMED_QUE_SET_NEXTIFERROR: {
		uint val = va_arg(va, uint);
		qu->next_if_err = val;
		goto end;
	}
	case FMED_QUE_SET_REPEATALL: {
		uint val = va_arg(va, uint);
		qu->repeat = val;
		goto end;
	}
	case FMED_QUE_SET_QUITIFDONE: {
		uint val = va_arg(va, uint);
		qu->quit_if_done = val;
		goto end;
	}

	case FMED_QUE_EXPAND2: {
		void *_e = va_arg(va, void*);
		void *ondone = va_arg(va, void*);
		void *ctx = va_arg(va, void*);
		entry *e = FF_GETPTR(entry, e, _e);
		r = (ffsize)que_expand2(e, ondone, ctx);
		goto end;
	}

	case FMED_QUE_EXPAND_ALL:
		pl_expand_next(qu->curlist, NULL);
		goto end;


	case FMED_QUE_SAVE: {
		int plid = va_arg(va, int);
		const char *fn = va_arg(va, char*);
		plist *pl = plist_by_useridx(plid);
		if (pl == NULL) {
			r = -1;
			goto end;
		}
		r = (ffsize)que_export(pl, fn);
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
		pl = pl_default();
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
		pl = pl_default();
		if (pl->cur != NULL && pl->cur->refcount != 0)
			pl->cur->stop_after = 1;
		break;

	case FMED_QUE_NEXT2:
	case FMED_QUE_PREV2:
		pl = pl_default();
		e = pl->cur;
		if (param != NULL) {
			e = FF_GETPTR(entry, e, param);
			pl = e->plist;
		} else {
			qu->track->cmd(NULL, FMED_TRACK_STOPALL);
		}

		if (cmd == FMED_QUE_NEXT2) {
			if (NULL == (pl->cur = que_getnext(e)))
				break;
		} else {
			if (NULL == (pl->cur = pl_prev(pl, e))) {
				dbglog(core, NULL, "que", "no previous file in playlist");
				break;
			}
		}

		que_play(pl->cur);
		break;

	case FMED_QUE_ADD:
		return (ssize_t)que_add(qu->curlist, param, NULL, flags);

	case FMED_QUE_EXPAND: {
		void *r = param;
		e = FF_GETPTR(entry, e, r);
		fmed_track_obj *trk = qu->track->create(FMED_TRK_TYPE_EXPAND, e->e.url.ptr);
		if (trk == NULL || trk == FMED_TRK_EFMT)
			return (size_t)trk;
		ent_start_prepare(e, trk);
		qu->track->cmd(trk, FMED_TRACK_START);
		return (size_t)trk;
	}

	case FMED_QUE_RM:
		e = param;
		if (e == NULL)
			break;
		dbglog(core, NULL, "que", "removed item %S", &e->e.url);

		if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL) {
			e->e.list_index = que_cmdv(FMED_QUE_ID, &e->e);

			// remove item from index, but don't free the object yet
			e->refcount++;
			fflk_lock(&qu->plist_lock);
			ent_rm(e);
			fflk_unlock(&qu->plist_lock);
			e->refcount--;

			dbglog0("calling onchange(FMED_QUE_ONRM): index:%u"
				, e->e.list_index);
			qu->onchange(&e->e, FMED_QUE_ONRM);
		}

		fflk_lock(&qu->plist_lock);
		ent_rm(e);
		fflk_unlock(&qu->plist_lock);
		break;

	case FMED_QUE_RMDEAD: {
		int n = 0;
		fflist_item *it;
		FFLIST_FOR(ents, it) {
			e = FF_GETPTR(entry, sib, it);
			it = it->next;
			if (!fffile_exists(e->e.url.ptr)) {
				que_cmd(FMED_QUE_RM, e);
				n++;
			}
		}
		return n;
	}

	case FMED_QUE_METASET:
		{
		const ffstr *pair = (void*)param2;
		que_meta_set(param, &pair[0], &pair[1], flags >> 16);
		}
		break;

	case FMED_QUE_HAVEUSERMETA:
		e = param;
		return (e->meta.len != 0 || e->no_tmeta);

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

		if (n >= qu->plists.len)
			return -1;

		plist *sel;
		int prev = -1;
		fflist_item *li;
		FFLIST_FOREACH(&qu->plists, li) {
			plist *pl = FF_STRUCTPTR(plist, sib, li);
			if (pl == qu->curlist)
				prev = i;
			if (i++ == n)
				sel = pl;
		}
		qu->curlist = sel;
		return prev;
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
			*ent = (void*)que_cmdv(FMED_QUE_ITEM, -1, i);
			return 1;
		}
		//fallthrough

	case FMED_QUE_LIST_NOFILTER:
		return pl_list_next((fmed_que_entry**)param);

	case FMED_QUE_ISCURLIST:
		e = param;
		return (e->plist == qu->curlist);

	case FMED_QUE_ID:
		e = param;
		if (e == NULL)
			return -1;
		pl = e->plist;
		if (pl->filtered_plist != NULL)
			pl = pl->filtered_plist;
		return plist_ent_idx(pl, e);

	case FMED_QUE_ITEM: {
		int plid = (int)(size_t)param;
		pl = plist_by_useridx(plid);
		if (pl == NULL)
			return (ffsize)NULL;
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
		if (NULL == ffvec_growT(&pl->indexes, 16, entry*))
			return -1;
		*ffvec_pushT(&pl->indexes, entry*) = e;
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

/** Shift elements to the right.
A[0]...  ( A[i]... )  A[i+n]... ->
A[0]...  ( ... )  A[i]...
*/
static inline void _ffvec_shiftr(ffvec *v, size_t i, size_t n, size_t elsz)
{
	char *dst = v->ptr + (i + n) * elsz;
	const char *src = v->ptr + i * elsz;
	const char *end = v->ptr + v->len * elsz;
	ffmem_move(dst, src, end - src);
}

static fmed_que_entry* que_add(plist *pl, fmed_que_entry *ent, entry *prev, uint flags)
{
	entry *e = NULL;

	if (flags & FMED_QUE_ADD_DONE) {
		if (ent == NULL) {
			if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL) {
				dbglog0("calling onchange(FMED_QUE_ONADD_DONE)", 0);
				qu->onchange(NULL, FMED_QUE_ONADD_DONE);
			}
			return NULL;
		}
		e = FF_GETPTR(entry, e, ent);
		goto done;
	}

	e = ent_new(ent);
	if (e == NULL)
		return NULL;
	e->plist = pl;

	fflk_lock(&qu->plist_lock);

	ffchain_append(&e->sib, (prev != NULL) ? &prev->sib : fflist_last(&e->plist->ents));
	e->plist->ents.len++;
	if (NULL == ffvec_growT(&e->plist->indexes, 16, entry*)) {
		ent_rm(e);
		fflk_unlock(&qu->plist_lock);
		return NULL;
	}
	ssize_t i = e->plist->indexes.len;
	if (prev != NULL) {
		ssize_t i2 = plist_ent_idx(e->plist, prev);
		if (i2 != -1) {
			i = i2 + 1;
			_ffvec_shiftr(&e->plist->indexes, i, 1, sizeof(entry*));
		}
	}
	e->list_pos = i;
	((entry**)e->plist->indexes.ptr) [i] = e;
	e->plist->indexes.len++;
	fflk_unlock(&qu->plist_lock);

	dbglog0("added: [%L/%L] '%S' (%d: %d-%d) after:'%s'"
		, e->list_pos+1, e->plist->indexes.len
		, &ent->url
		, ent->dur, ent->from, ent->to
		, (prev != NULL) ? prev->url : NULL);

done:
	if (!(flags & FMED_QUE_NO_ONCHANGE) && qu->onchange != NULL)
		qu->onchange(&e->e, FMED_QUE_ONADD | (flags & FMED_QUE_MORE));
	return &e->e;
}
