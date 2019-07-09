/** Directory input.  M3U, PLS input.  CUE input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/data/m3u.h>
#include <FF/data/pls.h>
#include <FF/data/cue.h>
#include <FF/net/url.h>
#include <FF/path.h>
#include <FF/sys/dir.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


static const fmed_core *core;
const fmed_queue *qu;

typedef struct dirconf_t {
	byte expand;
} dirconf_t;
dirconf_t dirconf;

typedef struct m3u {
	ffm3u m3u;
	fmed_que_entry ent;
	ffpls_entry pls_ent;
	fmed_que_entry *qu_cur;
	uint fin :1;
} m3u;

typedef struct pls_in {
	ffpls pls;
	ffpls_entry pls_ent;
	fmed_que_entry *qu_cur;
} pls_in;

typedef struct cue {
	ffcuep cue;
	ffcue cu;
	fmed_que_entry ent;
	fmed_que_entry *qu_cur;

	ffarr gmetas;
	ffarr metas;
	uint nmeta;

	ffarr trackno;
	uint curtrk;

	uint have_gmeta :1;
	uint utf8 :1;
} cue;


//FMEDIA MODULE
static const void* plist_iface(const char *name);
static int plist_sig(uint signo);
static void plist_destroy(void);
static int plist_conf(const char *name, ffpars_ctx *ctx);
static const fmed_mod fmed_plist_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&plist_iface, &plist_sig, &plist_destroy, &plist_conf
};

//M3U INPUT
static void* m3u_open(fmed_filt *d);
static void m3u_close(void *ctx);
static int m3u_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_m3u_input = {
	&m3u_open, &m3u_process, &m3u_close
};

//PLS INPUT
static void* pls_open(fmed_filt *d);
static void pls_close(void *ctx);
static int pls_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_pls_input = {
	&pls_open, &pls_process, &pls_close
};

//CUE INPUT
static void* cue_open(fmed_filt *d);
static void cue_close(void *ctx);
static int cue_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_cue_input = {
	&cue_open, &cue_process, &cue_close
};

static int cue_trackno(cue *c, fmed_filt *d, ffarr *arr);

//DIR INPUT
static void* dir_open(fmed_filt *d);
static void dir_close(void *ctx);
static int dir_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_dir_input = {
	&dir_open, &dir_process, &dir_close
};

static int dir_conf(ffpars_ctx *ctx);
static int dir_open_r(const char *dirname, fmed_filt *d);

static int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);

static const ffpars_arg dir_conf_args[] = {
	{ "expand",  FFPARS_TBOOL8,  FFPARS_DSTOFF(dirconf_t, expand) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_plist_mod;
}


static const void* plist_iface(const char *name)
{
	if (!ffsz_cmp(name, "m3u"))
		return &fmed_m3u_input;
	else if (!ffsz_cmp(name, "pls"))
		return &fmed_pls_input;
	else if (!ffsz_cmp(name, "cue"))
		return &fmed_cue_input;
	else if (!ffsz_cmp(name, "dir"))
		return &fmed_dir_input;
	return NULL;
}

static int plist_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "dir"))
		return dir_conf(ctx);
	return -1;
}

static int plist_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		if (NULL == (qu = core->getmod("#queue.queue")))
			return 1;
		break;
	}
	return 0;
}

static void plist_destroy(void)
{
}


/** Get absolute filename. */
static int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst)
{
	const char *fn;
	ffstr path = {0};
	ffstr3 s = {0};

	if (0 == ffuri_scheme(name->ptr, name->len)
		&& !ffpath_abs(name->ptr, name->len)) {

		fn = d->track->getvalstr(d->trk, "input");
		if (NULL != ffpath_split2(fn, ffsz_len(fn), &path, NULL))
			path.len++;
	}

	if (0 == ffstr_catfmt(&s, "%S%S", &path, name))
		return 1;
	ffstr_acqstr3(dst, &s);
	return 0;
}


static void* m3u_open(fmed_filt *d)
{
	m3u *m;
	if (NULL == (m = ffmem_tcalloc1(m3u)))
		return NULL;
	ffm3u_init(&m->m3u);
	m->qu_cur = (void*)fmed_getval("queue_item");
	return m;
}

static void m3u_close(void *ctx)
{
	m3u *m = ctx;
	ffm3u_close(&m->m3u);
	ffpls_entry_free(&m->pls_ent);
	ffmem_free(m);
}

static int m3u_process(void *ctx, fmed_filt *d)
{
	m3u *m = ctx;
	int r, r2, fin = 0;
	ffstr data;

	ffstr_set(&data, d->data, d->datalen);
	d->datalen = 0;

	for (;;) {

	r = ffm3u_parse(&m->m3u, &data);

	if (r == FFPARS_MORE && (d->flags & FMED_FLAST)) {
		if (!fin) {
			fin = 1;
			ffstr_setcz(&data, "\n");
			continue;
		}

		qu->cmd(FMED_QUE_ADD | FMED_QUE_ADD_DONE, NULL);
		qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
		return FMED_RFIN;
	}

	r2 = ffm3u_entry_get(&m->pls_ent, r, &m->m3u.val);

	if (r2 == 1) {
		fmed_que_entry ent, *cur;
		ffstr meta[2];
		ffmem_tzero(&ent);

		if (0 != plist_fullname(d, (ffstr*)&m->pls_ent.url, &ent.url))
			return FMED_RERR;

		if (m->pls_ent.duration != -1)
			ent.dur = m->pls_ent.duration * 1000;

		cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER | FMED_QUE_NO_ONCHANGE, &ent, m->qu_cur);
		ffstr_free(&ent.url);
		qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, m->qu_cur);

		if (m->pls_ent.artist.len != 0) {
			ffstr_setcz(&meta[0], "artist");
			ffstr_set2(&meta[1], &m->pls_ent.artist);
			qu->cmd2(FMED_QUE_METASET | (FMED_QUE_TMETA << 16), cur, (size_t)meta);
		}

		if (m->pls_ent.title.len != 0) {
			ffstr_setcz(&meta[0], "title");
			ffstr_set2(&meta[1], &m->pls_ent.title);
			qu->cmd2(FMED_QUE_METASET | (FMED_QUE_TMETA << 16), cur, (size_t)meta);
		}

		qu->cmd2(FMED_QUE_ADD | FMED_QUE_MORE | FMED_QUE_ADD_DONE, cur, 0);
		m->qu_cur = cur;
	}

	if (r == FFPARS_MORE) {
		return FMED_RMORE;

	} else if (ffpars_iserr(-r)) {
		errlog(core, d->trk, "m3u", "parse error at line %u", m->m3u.line);
		return FMED_RERR;
	}

	}
}


static void* pls_open(fmed_filt *d)
{
	pls_in *p;
	if (NULL == (p = ffmem_tcalloc1(pls_in)))
		return NULL;
	ffpls_init(&p->pls);
	p->qu_cur = (void*)fmed_getval("queue_item");
	return p;
}

static void pls_close(void *ctx)
{
	pls_in *p = ctx;
	ffpls_close(&p->pls);
	ffpls_entry_free(&p->pls_ent);
	ffmem_free(p);
}

static int pls_process(void *ctx, fmed_filt *d)
{
	pls_in *p = ctx;
	int r, r2, fin = 0;
	ffstr data;

	ffstr_set(&data, d->data, d->datalen);
	d->datalen = 0;

	for (;;) {

	r = ffpls_parse(&p->pls, &data);

	if (r == FFPARS_MORE && (d->flags & FMED_FLAST)) {
		if (!fin) {
			fin = 1;
			ffstr_setcz(&data, "\n");
			continue;
		}

		r = FFPLS_FIN;
	}

	r2 = ffpls_entry_get(&p->pls_ent, r, &p->pls.val);

	if (r2 == 1) {
		fmed_que_entry ent, *cur;
		ffstr meta[2];
		ffmem_tzero(&ent);

		if (0 != plist_fullname(d, (ffstr*)&p->pls_ent.url, &ent.url))
			return FMED_RERR;

		if (p->pls_ent.duration != -1)
			ent.dur = p->pls_ent.duration * 1000;

		cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER | FMED_QUE_NO_ONCHANGE, &ent, p->qu_cur);
		ffstr_free(&ent.url);
		qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, p->qu_cur);

		if (p->pls_ent.title.len != 0) {
			ffstr_setcz(&meta[0], "title");
			ffstr_set2(&meta[1], &p->pls_ent.title);
			qu->cmd2(FMED_QUE_METASET | (FMED_QUE_TMETA << 16), cur, (size_t)meta);
		}

		qu->cmd2(FMED_QUE_ADD | FMED_QUE_ADD_DONE, cur, 0);
		p->qu_cur = cur;
	}

	if (r == FFPLS_FIN) {
		qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
		return FMED_RFIN;

	} else if (r == FFPARS_MORE) {
		return FMED_RMORE;

	} else if (ffpars_iserr(-r)) {
		errlog(core, d->trk, "pls", "parse error at line %u", p->pls.line);
		return FMED_RERR;
	}

	}
}


/** Convert a list of track numbers to array. */
static int cue_trackno(cue *c, fmed_filt *d, ffarr *arr)
{
	ffstr s;
	const char *val;
	if (FMED_PNULL == (val = d->track->getvalstr(d->trk, "input_trackno")))
		return 0;

	ffstr_setz(&s, val);

	while (s.len != 0) {
		size_t len = s.len;
		uint num;
		int i = ffs_numlist(s.ptr, &len, &num);
		ffstr_shift(&s, len);

		if (i < 0) {
			errlog(core, d->trk, "cue", "invalid value for --track");
			return -1;
		}

		uint *n = ffarr_push(arr, uint);
		if (n == NULL)
			return -1;
		*n = num;
	}

	ffint_sort((uint*)arr->ptr, arr->len, 0);
	return 0;
}

static const uint cue_opts[] = {
	FFCUE_GAPSKIP, FFCUE_GAPPREV, FFCUE_GAPPREV1, FFCUE_GAPCURR
};

static void* cue_open(fmed_filt *d)
{
	cue *c;
	uint64 val;
	if (NULL == (c = ffmem_tcalloc1(cue)))
		return NULL;

	if (0 != cue_trackno(c, d, &c->trackno)) {
		cue_close(c);
		return NULL;
	}

	uint gaps = FFCUE_GAPPREV;
	if (FMED_NULL != (int)(val = core->getval("cue_gaps"))) {
		if (val > FFCNT(cue_opts))
			errlog(core, d->trk, "cue", "cue_gaps value must be within 0..3 range");
		else
			gaps = cue_opts[val];
	}

	ffcue_init(&c->cue);
	c->qu_cur = (void*)fmed_getval("queue_item");
	c->cu.options = gaps;
	c->utf8 = 1;
	return c;
}

static void cue_close(void *ctx)
{
	cue *c = ctx;
	ffcue_close(&c->cue);
	ffstr_free(&c->ent.url);
	FFARR_FREE_ALL(&c->gmetas, ffarr_free, ffarr);
	FFARR_FREE_ALL(&c->metas, ffarr_free, ffarr);
	ffarr_free(&c->trackno);
	ffmem_free(c);
}

/** Find meta name in array.
Return -1 if not found. */
static ssize_t cue_meta_find(const ffarr *a, size_t n, const ffarr *search)
{
	const ffarr *m = (void*)a->ptr;
	for (size_t i = 0;  i != n;  i += 2) {
		if (ffstr_eq2(&m[i], search))
			return i;
	}
	return -1;
}

static int cue_process(void *ctx, fmed_filt *d)
{
	cue *c = ctx;
	size_t n;
	int rc = FMED_RERR, r, done = 0, fin = 0;
	ffarr *meta;
	ffstr metaname, in;
	ffarr val = {0}, *m;
	ffcuetrk *ctrk;
	uint codepage = core->getval("codepage");
	fmed_que_entry *cur;

	ffstr_set(&in, d->data, d->datalen);
	d->datalen = 0;

	while (!done) {
		n = in.len;
		r = ffcue_parse(&c->cue, in.ptr, &n);
		ffstr_shift(&in, n);

		if (r == FFPARS_MORE) {
			if (!(d->flags & FMED_FLAST)) {
				rc = FMED_RMORE;
				goto err;
			}

			if (fin) {
				// end of .cue file
				if (NULL == (ctrk = ffcue_index(&c->cu, FFCUE_FIN, 0)))
					break;
				done = 1;
				c->nmeta = c->metas.len;
				goto add;
			}

			fin = 1;
			ffstr_setcz(&in, "\n");
			continue;

		} else if (ffpars_iserr(-r)) {
			errlog(core, d->trk, "cue", "parse error at line %u: %s"
				, c->cue.line, ffpars_errstr(-r));
			goto err;
		}

		ffstr *v = &c->cue.val;
		if (c->utf8 && ffutf8_valid(v->ptr, v->len))
			ffarr_copy(&val, v->ptr, v->len);
		else {
			c->utf8 = 0;
			size_t len = v->len;
			size_t n = ffutf8_encode(NULL, 0, v->ptr, &len, codepage);
			if (NULL == ffarr_realloc(&val, n))
				goto err;
			val.len = ffutf8_encode(val.ptr, val.cap, v->ptr, &len, codepage);
		}

		switch (r) {
		case FFCUE_TITLE:
			ffstr_setcz(&metaname, "album");
			goto add_metaname;

		case FFCUE_TRACKNO:
			c->nmeta = c->metas.len;
			ffstr_setcz(&metaname, "tracknumber");
			goto add_metaname;

		case FFCUE_TRK_TITLE:
			ffstr_setcz(&metaname, "title");
			goto add_metaname;

		case FFCUE_TRK_PERFORMER:
			ffstr_setcz(&metaname, "artist");
			goto add_metaname;

		case FFCUE_PERFORMER:
			ffstr_setcz(&metaname, "artist");

add_metaname:
			if (NULL == (meta = ffarr_pushT(&c->metas, ffarr)))
				goto err;
			ffstr_set2(meta, &metaname);
			meta->cap = 0;
			// break;

		case FFCUE_REM_VAL:
			if (NULL == (meta = ffarr_pushT(&c->metas, ffarr)))
				goto err;
			ffarr_acq(meta, &val);
			break;

		case FFCUE_REM_NAME:
			if (NULL == (meta = ffarr_pushT(&c->metas, ffarr)))
				goto err;
			ffarr_acq(meta, &val);
			break;

		case FFCUE_FILE:
			if (!c->have_gmeta) {
				c->have_gmeta = 1;
				c->gmetas = c->metas;
				ffarr_null(&c->metas);
			}
			ffstr_free(&c->ent.url);
			if (0 != plist_fullname(d, (ffstr*)&val, &c->ent.url))
				goto err;
			break;
		}

		if (NULL == (ctrk = ffcue_index(&c->cu, r, (int)c->cue.intval)))
			continue;

add:
		c->curtrk++;
		if (c->trackno.len != 0) {
			size_t n;
			if (0 > (int)(n = ffint_binfind4((uint*)c->trackno.ptr, c->trackno.len, c->curtrk)))
				goto next;
			if (n == c->trackno.len - 1)
				done = 1;
		}

		if (ctrk->to != 0 && ctrk->from >= ctrk->to) {
			errlog(core, d->trk, "cue", "invalid INDEX values");
			continue;
		}

		c->ent.from = -(int)ctrk->from;
		c->ent.to = -(int)ctrk->to;
		c->ent.dur = (ctrk->to != 0) ? (ctrk->to - ctrk->from) * 1000 / 75 : 0;

		cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER | FMED_QUE_NO_ONCHANGE, &c->ent, c->qu_cur);
		qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, c->qu_cur);

		// add global meta that isn't set in TRACK context
		m = (void*)c->gmetas.ptr;
		for (uint i = 0;  i != c->gmetas.len;  i += 2) {

			if (cue_meta_find(&c->metas, c->nmeta, &m[i]) >= 0)
				continue;

			ffstr pair[2];
			ffstr_set2(&pair[0], &m[i]);
			ffstr_set2(&pair[1], &m[i + 1]);
			qu->cmd2(FMED_QUE_METASET, cur, (size_t)pair);
		}

		// add TRACK meta
		m = (void*)c->metas.ptr;
		for (uint i = 0;  i != c->nmeta;  i += 2) {
			ffstr pair[2];
			ffstr_set2(&pair[0], &m[i]);
			ffstr_set2(&pair[1], &m[i + 1]);
			qu->cmd2(FMED_QUE_METASET, cur, (size_t)pair);
		}

		qu->cmd2(FMED_QUE_ADD | FMED_QUE_ADD_DONE, cur, 0);
		c->qu_cur = cur;

next:
		/* 'metas': TRACK_N TRACK_N+1
		Remove the items for TRACK_N. */
		m = (void*)c->metas.ptr;
		for (uint i = 0;  i != c->nmeta;  i++) {
			ffarr_free(&m[i]);
		}
		_ffarr_rm(&c->metas, 0, c->nmeta, sizeof(ffarr));
		c->nmeta = c->metas.len;
	}

	qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
	rc = FMED_RFIN;

err:
	ffarr_free(&val);
	return rc;
}


static int dir_conf(ffpars_ctx *ctx)
{
	dirconf.expand = 1;
	ffpars_setargs(ctx, &dirconf, dir_conf_args, FFCNT(dir_conf_args));
	return 0;
}

static void* dir_open(fmed_filt *d)
{
	ffdirexp dr;
	const char *fn, *dirname;
	fmed_que_entry e, *first, *cur;

	if (FMED_PNULL == (dirname = d->track->getvalstr(d->trk, "input")))
		return NULL;

	if (dirconf.expand) {
		dir_open_r(dirname, d);
		return FMED_FILT_DUMMY;
	}

	if (0 != ffdir_expopen(&dr, (char*)dirname, 0)) {
		if (fferr_last() != ENOMOREFILES) {
			syserrlog(core, d->trk, "dir", "%s", ffdir_open_S);
			return NULL;
		}
		return FMED_FILT_DUMMY;
	}

	first = (void*)fmed_getval("queue_item");
	cur = first;

	while (NULL != (fn = ffdir_expread(&dr))) {
		ffmem_tzero(&e);
		ffstr_setz(&e.url, fn);
		void *prev = cur;
		cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER, &e, prev);
		qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, prev);
	}

	ffdir_expclose(&dr);
	qu->cmd(FMED_QUE_RM, first);
	return FMED_FILT_DUMMY;
}

/**
'include' filter matches files only.
'exclude' filter matches files & directories.
Return TRUE if filename matches user's filename wildcards. */
static ffbool file_matches(fmed_filt *d, const char *fn, ffbool dir)
{
	size_t fnlen = ffsz_len(fn);
	const ffstr *wc;
	ffbool ok = 1;

	if (!dir) {
		ok = (d->include_files.len == 0);
		FFARR_WALKT(&d->include_files, wc, ffstr) {
			if (0 == ffs_wildcard(wc->ptr, wc->len, fn, fnlen, FFS_WC_ICASE)) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			return 0;
	}

	FFARR_WALKT(&d->exclude_files, wc, ffstr) {
		if (0 == ffs_wildcard(wc->ptr, wc->len, fn, fnlen, FFS_WC_ICASE)) {
			ok = 0;
			break;
		}
	}
	return ok;
}

struct dir_ent {
	char *dir;
	ffchain_item sib;
};

/*
. Scan directory
. Add files to queue;  gather all directories into chain
. Get next directory from chain and scan it;  new directories will be added after this directory

Example:
.:
 (dir1)
 (dir2)
 file
dir1:
 (dir1/dir11)
 dir1/file
 dir1/dir11/file
dir2:
 dir2/file
*/
static int dir_open_r(const char *dirname, fmed_filt *d)
{
	ffdirexp dr;
	const char *fn;
	fmed_que_entry e, *first, *prev_qent;
	fffileinfo fi;
	ffchain chain;
	ffchain_item *lprev, *lcur;
	struct dir_ent *de;
	ffchain mblocks;
	ffmblk *mblk;
	ffchain_item *it;

	ffchain_init(&mblocks);

	ffchain_init(&chain);
	lprev = ffchain_sentl(&chain);
	lcur = lprev;

	first = (void*)fmed_getval("queue_item");
	prev_qent = first;

	for (;;) {

		dbglog(core, d->trk, NULL, "scanning %s", dirname);

		if (0 != ffdir_expopen(&dr, (char*)dirname, 0)) {
			if (fferr_last() != ENOMOREFILES)
				syserrlog(core, d->trk, NULL, "%s: %s", ffdir_open_S, dirname);
			goto next;
		}

		while (NULL != (fn = ffdir_expread(&dr))) {

			if (0 != fffile_infofn(fn, &fi)) {
				syserrlog(core, d->trk, NULL, "%s: %s", fffile_info_S, fn);
				continue;
			}

			if (!file_matches(d, ffdir_expname(&dr, fn), fffile_isdir(fffile_infoattr(&fi))))
				continue;

			if (fffile_isdir(fffile_infoattr(&fi))) {

				mblk = ffmblk_chain_last(&mblocks);
				if (mblk == NULL || ffarr_unused(&mblk->buf) == 0) {

					// allocate a new block with fixed size = 4kb
					if (NULL == (mblk = ffmblk_chain_push(&mblocks))
						|| NULL == ffarr_allocT(&mblk->buf, 4096 / sizeof(struct dir_ent), struct dir_ent)) {
						syserrlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
						goto end;
					}
				}

				de = ffarr_pushT(&mblk->buf, struct dir_ent);
				if (NULL == (de->dir = ffsz_alcopyz(fn))) {
					syserrlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
					goto end;
				}
				ffchain_append(&de->sib, lprev);
				lprev = &de->sib;
				continue;
			}

			ffmem_tzero(&e);
			ffstr_setz(&e.url, fn);
			void *cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER, &e, prev_qent);
			qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, prev_qent);
			prev_qent = cur;
		}

		ffdir_expclose(&dr);
		ffmem_tzero(&dr);

next:
		lcur = lcur->next;
		if (lcur == ffchain_sentl(&chain))
			break;
		de = FF_GETPTR(struct dir_ent, sib, lcur);
		dirname = de->dir;
		lprev = lcur;
	}

end:
	ffdir_expclose(&dr);
	FFCHAIN_FOR(&mblocks, it) {
		mblk = FF_GETPTR(ffmblk, sib, it);
		it = it->next;
		FFARR_WALKT(&mblk->buf, de, struct dir_ent) {
			ffmem_safefree(de->dir);
		}
		ffmblk_free(mblk);
	}
	qu->cmd(FMED_QUE_RM, first);
	return 0;
}

static void dir_close(void *ctx)
{
}

static int dir_process(void *ctx, fmed_filt *d)
{
	return FMED_RFIN;
}
