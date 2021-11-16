/** fmedia: .pls read
2021, Simon Zolin */

#include <avpack/pls.h>

typedef struct pls_in {
	plsread pls;
	pls_entry pls_ent;
	fmed_que_entry *qu_cur;
} pls_in;

static void* pls_open(fmed_filt *d)
{
	pls_in *p;
	if (NULL == (p = ffmem_tcalloc1(pls_in)))
		return NULL;
	plsread_open(&p->pls);
	p->qu_cur = (void*)fmed_getval("queue_item");
	qu->cmdv(FMED_QUE_RM, p->qu_cur);
	return p;
}

static void pls_close(void *ctx)
{
	pls_in *p = ctx;
	plsread_close(&p->pls);
	pls_entry_free(&p->pls_ent);
	ffmem_free(p);
}

static int pls_add(pls_in *p, fmed_filt *d)
{
	fmed_que_entry ent = {}, *cur;

	if (0 != plist_fullname(d, (ffstr*)&p->pls_ent.url, &ent.url))
		return 1;

	if (p->pls_ent.duration != -1)
		ent.dur = p->pls_ent.duration * 1000;

	cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER | FMED_QUE_NO_ONCHANGE, &ent, p->qu_cur);
	ffstr_free(&ent.url);
	qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, p->qu_cur);

	if (p->pls_ent.title.len != 0) {
		ffstr meta[2];
		ffstr_setcz(&meta[0], "title");
		ffstr_set2(&meta[1], &p->pls_ent.title);
		qu->cmd2(FMED_QUE_METASET | (FMED_QUE_TMETA << 16), cur, (size_t)meta);
	}

	qu->cmd2(FMED_QUE_ADD | FMED_QUE_ADD_DONE, cur, 0);
	p->qu_cur = cur;

	pls_entry_free(&p->pls_ent);
	return 0;
}

static int pls_process(void *ctx, fmed_filt *d)
{
	pls_in *p = ctx;
	int r, fin = 0, commit = 0;
	ffstr data, val;
	ffuint trk_idx = (ffuint)-1, idx;

	ffstr_set(&data, d->data, d->datalen);
	d->datalen = 0;

	for (;;) {

		r = plsread_process(&p->pls, &data, &val, &idx);

		switch (r) {
		case PLSREAD_MORE:
			if (!(d->flags & FMED_FLAST)) {
				ffarr_copyself(&p->pls_ent.url);
				ffarr_copyself(&p->pls_ent.title);
				return FMED_RMORE;
			}

			if (!fin) {
				fin = 1;
				ffstr_setcz(&data, "\n");
				continue;
			}

			commit = 1;
			break;

		case PLSREAD_URL:
		case PLSREAD_TITLE:
		case PLSREAD_DURATION:
			if (trk_idx == (ffuint)-1) {
				trk_idx = idx;
			} else if (idx != trk_idx) {
				trk_idx = idx;
				commit = 1;
			}
			break;

		case PLSREAD_WARN:
			warnlog(core, d->trk, "pls", "parse error: %s", plsread_error(&p->pls));
			continue;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}

		if (commit) {
			commit = 0;
			if (p->pls_ent.url.len != 0 && 0 != pls_add(p, d))
				return FMED_RERR;
			if (fin)
				return FMED_RFIN;
		}

		switch (r) {
		case PLSREAD_URL:
			ffstr_set2(&p->pls_ent.url, &val);
			break;

		case PLSREAD_TITLE:
			ffstr_set2(&p->pls_ent.title, &val);
			break;

		case PLSREAD_DURATION:
			p->pls_ent.duration = plsread_duration_sec(&p->pls);
			break;
		}
	}
}

static const fmed_filter fmed_pls_input = {
	pls_open, pls_process, pls_close
};
