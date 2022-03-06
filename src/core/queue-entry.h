/** fmedia: functions for 1 entry in queue
2020, Simon Zolin */

struct entry {
	fmed_que_entry e;
	fflist_item sib;

	plist *plist;
	fmed_trk *trk;
	ffslice meta; //ffstr[]
	ffslice tmeta; //ffstr[]. transient meta - reset before every start of this item.
	ffslice dict; //ffstr[]

	size_t list_pos; //position number within playlist.  May be invalid.
	uint refcount;
	uint rm :1 // marked to remove
		, stop_after :1
		, no_tmeta :1
		, trk_stopped :1
		, trk_err :1
		, trk_mixed :1
		;

	char url[0];
};

static entry* ent_new(const fmed_que_entry *info)
{
	entry *e = ffmem_alloc(sizeof(entry) + info->url.len + 1);
	if (e == NULL)
		return NULL;
	ffmem_zero_obj(e);

	ffsz_copy(e->url, info->url.len + 1, info->url.ptr, info->url.len);
	e->e.url.ptr = e->url;
	e->e.url.len = info->url.len;

	e->e.from = info->from;
	e->e.to = info->to;
	e->e.dur = info->dur;
	return e;
}

static void ent_free(entry *e)
{
	FFSLICE_FOREACH_T(&e->meta, ffstr_free, ffstr);
	ffslice_free(&e->meta);
	FFSLICE_FOREACH_T(&e->dict, ffstr_free, ffstr);
	ffslice_free(&e->dict);
	FFSLICE_FOREACH_T(&e->tmeta, ffstr_free, ffstr);
	ffslice_free(&e->tmeta);

	ffmem_free(e->trk);
	ffmem_free(e);
}

static void ent_rm(entry *e)
{
	ffbool unindex_once = !e->rm;
	if (e->refcount != 0) {
		e->rm = 1;
		plist_remove_entry(e, unindex_once, 0);
		return;
	}

	plist_remove_entry(e, unindex_once, 1);
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

static void _que_meta_set(fmed_que_entry *ent, const char *name, size_t name_len, const char *val, size_t val_len, uint flags);
static void que_meta_set(fmed_que_entry *ent, const ffstr *name, const ffstr *val, uint flags);

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
			if (0 != fffile_readwhole(fn, &buf, -1)) {
				syserrlog("%s: %s", fffile_read_S, fn);
				goto end;
			}
			ffstr_acqstr3(&val, (ffarr*)&buf);
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
	que_meta_set(ent, &pair[0], &pair[1], flags);
}

static void que_meta_set(fmed_que_entry *ent, const ffstr *name, const ffstr *val, uint flags)
{
	entry *e = FF_GETPTR(entry, e, ent);
	char *sname, *sval;
	ffslice *a;

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
			ffvec ar;
			ffstr_set((ffstr*)&ar, (void*)a->ptr, a->len);
			ar.cap = a->len;
			ffslice_rmT((ffslice*)&ar, i, 2, ffstr);
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
	if (NULL == ffslice_growT(a, 2, ffstr)) {
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
		const ffslice *meta = (k == 0) ? &e->meta : &e->tmeta;
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

static void que_copytrackprops(entry *e, entry *src)
{
	if (src->trk != NULL)
		que_cmdv(FMED_QUE_SETTRACKPROPS, &e->e, src->trk);

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
}
