/** M3U, PLS input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/data/m3u.h>
#include <FF/data/pls.h>
#include <FF/net/url.h>
#include <FF/path.h>
#include <FF/sys/dir.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


const fmed_core *core;
const fmed_queue *qu;


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

int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_plist_mod;
}

extern const fmed_filter fmed_cue_input;
extern const fmed_filter fmed_dir_input;
extern int dir_conf(ffpars_ctx *ctx);

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
int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst)
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
