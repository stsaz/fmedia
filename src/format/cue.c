/** CUE input.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <FF/data/cue.h>
#include <FF/data/utf8.h>


extern const fmed_core *core;
extern const fmed_queue *qu;
extern int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);

//CUE INPUT
static void* cue_open(fmed_filt *d);
static void cue_close(void *ctx);
static int cue_process(void *ctx, fmed_filt *d);
const fmed_filter fmed_cue_input = {
	&cue_open, &cue_process, &cue_close
};

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

static int cue_trackno(cue *c, fmed_filt *d, ffarr *arr);


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
	uint val;
	if (NULL == (c = ffmem_tcalloc1(cue)))
		return NULL;

	if (0 != cue_trackno(c, d, &c->trackno)) {
		cue_close(c);
		return NULL;
	}

	uint gaps = FFCUE_GAPPREV;
	if (-1 != (int)(val = d->cue.gaps)) {
		if (val > FFCNT(cue_opts)) {
			errlog(core, d->trk, "cue", "cue_gaps value must be within 0..3 range");
			cue_close(c);
			return NULL;
		} else
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
			size_t n = ffutf8_from_cp(NULL, 0, v->ptr, v->len, codepage);
			if (NULL == ffarr_realloc(&val, n))
				goto err;
			val.len = ffutf8_from_cp(val.ptr, val.cap, v->ptr, v->len, codepage);
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
