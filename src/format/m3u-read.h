/** fmedia: .m3u read
2021, Simon Zolin */

#include <avpack/m3u.h>

typedef struct m3u {
	m3uread m3u;
	fmed_que_entry ent;
	pls_entry pls_ent;
	fmed_que_entry *qu_cur;
	uint fin :1;
	uint m3u_removed :1;
} m3u;

static void* m3u_open(fmed_filt *d)
{
	m3u *m;
	if (NULL == (m = ffmem_tcalloc1(m3u)))
		return NULL;
	m3uread_open(&m->m3u);
	m->qu_cur = (void*)fmed_getval("queue_item");
	return m;
}

static void m3u_close(void *ctx)
{
	m3u *m = ctx;
	if (!m->m3u_removed) {
		qu->cmdv(FMED_QUE_RM, m->qu_cur);
	}
	m3uread_close(&m->m3u);
	pls_entry_free(&m->pls_ent);
	ffmem_free(m);
}

static int m3u_add(m3u *m, fmed_filt *d)
{
	fmed_que_entry ent = {}, *cur;
	ffstr meta[2];

	if (0 != plist_fullname(d, (ffstr*)&m->pls_ent.url, &ent.url))
		return 1;

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
	if (!m->m3u_removed) {
		m->m3u_removed = 1;
		qu->cmdv(FMED_QUE_RM, m->qu_cur);
	}
	m->qu_cur = cur;
	pls_entry_free(&m->pls_ent);
	return 0;
}

static int m3u_process(void *ctx, fmed_filt *d)
{
	m3u *m = ctx;
	int r, fin = 0;
	ffstr data, val;

	ffstr_set(&data, d->data, d->datalen);
	d->datalen = 0;

	for (;;) {

		r = m3uread_process(&m->m3u, &data, &val);

		switch (r) {
		case M3UREAD_MORE:
			if (!(d->flags & FMED_FLAST)) {
				ffarr_copyself(&m->pls_ent.artist);
				ffarr_copyself(&m->pls_ent.title);
				return FMED_RMORE;
			}

			if (!fin) {
				fin = 1;
				ffstr_setcz(&data, "\n");
				continue;
			}

			qu->cmd(FMED_QUE_ADD | FMED_QUE_ADD_DONE, NULL);
			return FMED_RFIN;

		case M3UREAD_ARTIST:
			ffstr_set2(&m->pls_ent.artist, &val);
			break;

		case M3UREAD_TITLE:
			ffstr_set2(&m->pls_ent.title, &val);
			break;

		case M3UREAD_DURATION:
			m->pls_ent.duration = m3uread_duration_sec(&m->m3u);
			break;

		case M3UREAD_URL:
			ffstr_set2(&m->pls_ent.url, &val);
			if (0 != m3u_add(m, d))
				return FMED_RERR;
			break;

		case M3UREAD_WARN:
			warnlog(core, d->trk, "m3u", "parse error: %s", m3uread_error(&m->m3u));
			continue;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}
	}
}

static const fmed_filter fmed_m3u_input = {
	&m3u_open, &m3u_process, &m3u_close
};
