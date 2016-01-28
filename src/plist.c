/** Directory input.  M3U input.  CUE input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/data/m3u.h>
#include <FF/data/cue.h>
#include <FF/path.h>
#include <FF/dir.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


static const fmed_core *core;
const fmed_queue *qu;

typedef struct m3u {
	ffparser p;
	fmed_que_entry ent;
	struct { FFARR(ffstr) } metas;
	struct { FFARR(byte) } fmeta;
	uint furl :1;
} m3u;

typedef struct cue {
	ffparser p;
	fmed_que_entry ent;
	struct { FFARR(ffstr) } metas;
	ffarr trackno;
	uint curtrk;
	uint gmeta;
	uint gaps; //enum FFCUE_GAP
} cue;


//FMEDIA MODULE
static const void* plist_iface(const char *name);
static int plist_sig(uint signo);
static void plist_destroy(void);
static const fmed_mod fmed_plist_mod = {
	&plist_iface, &plist_sig, &plist_destroy
};

//M3U INPUT
static void* m3u_open(fmed_filt *d);
static void m3u_close(void *ctx);
static int m3u_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_m3u_input = {
	&m3u_open, &m3u_process, &m3u_close
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

static void m3u_reset(m3u *m);
static void m3u_copy(m3u *m);
static int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_plist_mod;
}


static const void* plist_iface(const char *name)
{
	if (!ffsz_cmp(name, "m3u"))
		return &fmed_m3u_input;
	else if (!ffsz_cmp(name, "cue"))
		return &fmed_cue_input;
	else if (!ffsz_cmp(name, "dir"))
		return &fmed_dir_input;
	return NULL;
}

static int plist_sig(uint signo)
{
	switch (signo) {
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


static int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst)
{
	const char *fn;
	ffstr path = {0};
	ffstr3 s = {0};
	if (!ffpath_abs(name->ptr, name->len)) {
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
	ffpars_init(&m->p);
	ffmem_tzero(&m->ent);
	return m;
}

static void m3u_close(void *ctx)
{
	m3u *m = ctx;
	ffpars_free(&m->p);
	m3u_reset(m);
	ffarr_free(&m->metas);
	ffarr_free(&m->fmeta);
	ffmem_free(m);
}

static void m3u_reset(m3u *m)
{
	ffstr *meta;
	uint i = 0;

	if (m->furl)
		ffstr_free(&m->ent.url);

	FFARR_WALK(&m->metas, meta) {
		if (m->fmeta.ptr[i++] == 1)
			ffstr_free(meta);
	}
	m->metas.len = 0;
	m->fmeta.len = 0;

	ffmem_tzero(&m->ent);
}

static void m3u_copy(m3u *m)
{
	uint i = 0;
	ffstr *meta;

	if (!m->furl && m->ent.url.len != 0) {
		ffstr_alcopystr(&m->ent.url, &m->ent.url);
		m->furl = 1;
	}

	FFARR_WALK(&m->metas, meta) {
		if (!m->fmeta.ptr[i]) {
			ffstr_alcopystr(meta, meta);
			m->fmeta.ptr[i] = 1;
		}
		i++;
	}
}

static int m3u_process(void *ctx, fmed_filt *d)
{
	m3u *m = ctx;
	size_t n;
	int r;
	ffstr metaname;

	for (;;) {
		n = d->datalen;
		r = ffm3u_parse(&m->p, d->data, &n);
		d->data += n;
		d->datalen -= n;

		if (r == FFPARS_MORE)
			break;
		else if (ffpars_iserr(r)) {
			errlog(core, d->trk, "m3u", "parse error at line %u", m->p.line);
			return FMED_RERR;
		}

		switch (m->p.type) {
		case FFM3U_DUR:
			if (m->p.intval != -1)
				m->ent.dur = m->p.intval * 1000;
			break;

		case FFM3U_ARTIST:
			ffstr_setcz(&metaname, "artist");
			goto add_meta;

		case FFM3U_TITLE:
			ffstr_setcz(&metaname, "title");
add_meta:
			if (NULL == ffarr_grow(&m->metas, 2, 0)
				|| NULL == ffarr_grow(&m->fmeta, 2, 0))
				return FMED_RERR;

			*ffarr_push(&m->metas, ffstr) = metaname;
			*ffarr_push(&m->fmeta, byte) = 0;

			*ffarr_push(&m->metas, ffstr) = m->p.val;
			*ffarr_push(&m->fmeta, byte) = 0;
			break;

		case FFM3U_NAME:
			if (0 != plist_fullname(d, &m->p.val, &m->ent.url))
				return FMED_RERR;
			m->furl = 1;
			m->ent.meta = m->metas.ptr;
			m->ent.nmeta = m->metas.len;
			qu->add(&m->ent);

			m3u_reset(m);
			break;
		}
	}

	if (d->flags & FMED_FLAST) {
		qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
		return FMED_RFIN;
	}

	m3u_copy(m);

	return FMED_RMORE;
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
	int64 val;
	if (NULL == (c = ffmem_tcalloc1(cue)))
		return NULL;

	if (0 != cue_trackno(c, d, &c->trackno)) {
		cue_close(c);
		return NULL;
	}

	c->gaps = FFCUE_GAPPREV;
	if (FMED_NULL != (val = core->getval("cue_gaps"))) {
		if (val > FFCNT(cue_opts))
			errlog(core, d->trk, "cue", "cue_gaps value must be within 0..3 range");
		else
			c->gaps = cue_opts[val];
	}

	ffpars_init(&c->p);
	return c;
}

static void cue_close(void *ctx)
{
	cue *c = ctx;
	ffpars_free(&c->p);
	ffstr_free(&c->ent.url);
	ffarr_free(&c->metas);
	ffarr_free(&c->trackno);
	ffmem_free(c);
}

static int cue_process(void *ctx, fmed_filt *d)
{
	cue *c = ctx;
	size_t n;
	int rc = FMED_RERR, r, done = 0;
	ffstr *meta;
	ffstr metaname;
	ffarr val = {0};
	ffcue cu = {0};
	ffcuetrk *ctrk;
	uint codepage = core->getval("codepage");
	uint utf8 = 1, have_glob_artist = 0, artist_trk = 0;

	cu.options = c->gaps;

	while (!done) {
		n = d->datalen;
		r = ffcue_parse(&c->p, d->data, &n);
		d->data += n;
		d->datalen -= n;

		if (r == FFPARS_MORE) {
			// end of .cue file
			if (NULL == (ctrk = ffcue_index(&cu, FFCUE_FIN, 0)))
				break;
			done = 1;
			c->ent.nmeta = c->metas.len;
			goto add;

		} else if (ffpars_iserr(r)) {
			errlog(core, d->trk, "cue", "parse error at line %u: %s"
				, c->p.line, ffpars_errstr(r));
			goto err;
		}

		if (utf8 && ffutf8_valid(c->p.val.ptr, c->p.val.len))
			ffarr_copy(&val, c->p.val.ptr, c->p.val.len);
		else {
			utf8 = 0;
			size_t len = c->p.val.len;
			size_t n = ffutf8_encode(NULL, 0, c->p.val.ptr, &len, codepage);
			if (NULL == ffarr_realloc(&val, n))
				goto err;
			val.len = ffutf8_encode(val.ptr, val.cap, c->p.val.ptr, &len, codepage);
		}

		switch (c->p.type) {
		case FFCUE_TITLE:
			ffstr_setcz(&metaname, "album");
			goto add_metaname;

		case FFCUE_TRACKNO:
			c->ent.nmeta = c->metas.len;
			ffstr_setcz(&metaname, "tracknumber");
			goto add_metaname;

		case FFCUE_TRK_TITLE:
			ffstr_setcz(&metaname, "title");
			goto add_metaname;

		case FFCUE_TRK_PERFORMER:
			artist_trk++;
		case FFCUE_PERFORMER:
			ffstr_setcz(&metaname, "artist");

add_metaname:
			if (NULL == (meta = ffarr_push(&c->metas, ffstr)))
				goto err;
			*meta = metaname;
			// break;

		case FFCUE_REM_VAL:
			if (NULL == (meta = ffarr_push(&c->metas, ffstr)))
				goto err;
			ffstr_acqstr3(meta, &val);
			break;

		case FFCUE_REM_NAME:
			if (NULL == (meta = ffarr_push(&c->metas, ffstr)))
				goto err;
			ffstr_acqstr3(meta, &val);
			break;

		case FFCUE_FILE:
			if (c->gmeta == 0)
				c->gmeta = c->metas.len;
			ffstr_free(&c->ent.url);
			if (0 != plist_fullname(d, (ffstr*)&val, &c->ent.url))
				goto err;
			break;
		}

		if (c->p.type == FFCUE_PERFORMER) {
			/* swap {FIRST_ENTRY_NAME, FIRST_ENTRY_VAL} <-> {"artist", ARTIST_VAL}
			This allows to easily skip global artist key-value pair when track artist name is specified. */
			have_glob_artist = 1;
			_ffarr_swap(c->metas.ptr, c->metas.ptr + c->metas.len - 2, 2, sizeof(ffstr));
		}

		if (NULL == (ctrk = ffcue_index(&cu, c->p.type, (int)c->p.intval)))
			continue;

add:
		c->curtrk++;
		if (c->trackno.len != 0) {
			ssize_t n;
			if (-1 == (n = ffint_binfind4((uint*)c->trackno.ptr, c->trackno.len, c->curtrk)))
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

		if (have_glob_artist && artist_trk != 0) {
			// remove global artist
			struct { FFARR(ffstr) } metas;
			ffarr_set(&metas, c->metas.ptr, c->metas.len);
			ffarr_shift(&metas, 2);
			c->ent.nmeta -= 2;
			c->ent.meta = metas.ptr;
			qu->add(&c->ent);
			c->ent.nmeta += 2;
			artist_trk--;
			goto next;
		}

		c->ent.meta = c->metas.ptr;
		qu->add(&c->ent);

next:
		/* 'metas': GLOBAL TRACK_N TRACK_N+1
		Remove the items for TRACK_N. */
		ffmemcpy(c->metas.ptr + c->gmeta, c->metas.ptr + c->ent.nmeta, (c->metas.len - c->ent.nmeta) * sizeof(ffstr));
		c->metas.len = c->gmeta + c->metas.len - c->ent.nmeta;
	}

	qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
	rc = FMED_RFIN;

err:
	ffarr_free(&val);
	return rc;
}


static void* dir_open(fmed_filt *d)
{
	ffdirexp dr;
	const char *fn, *dirname;
	fmed_que_entry e;

	if (FMED_PNULL == (dirname = d->track->getvalstr(d->trk, "input")))
		return NULL;

	if (0 != ffdir_expopen(&dr, (char*)dirname, 0)) {
		if (fferr_last() != ENOMOREFILES)
			syserrlog(core, d->trk, "dir", "%e", FFERR_DIROPEN);
		return NULL;
	}

	while (NULL != (fn = ffdir_expread(&dr))) {
		ffmem_tzero(&e);
		ffstr_setz(&e.url, fn);
		qu->add(&e);
	}

	ffdir_expclose(&dr);
	qu->cmd(FMED_QUE_RM, (void*)fmed_getval("queue_item"));
	return FMED_FILT_DUMMY;
}

static void dir_close(void *ctx)
{
}

static int dir_process(void *ctx, fmed_filt *d)
{
	return FMED_RFIN;
}
