/** fmedia: .m3u write
2021, Simon Zolin */

#include <avpack/m3u.h>

struct m3uw {
	m3uwrite m3;
	fmed_que_entry *cur;
	const fmed_track *track;
	void *trk;
};

static void* m3uw_open(fmed_filt *d)
{
	struct m3uw *m = ffmem_new(struct m3uw);
	m->trk = d->trk;
	m->track = d->track;
	m3uwrite_create(&m->m3, 0);
	return m;
}

static void m3uw_close(void *ctx)
{
	struct m3uw *m = ctx;
	m3uwrite_close(&m->m3);
	ffmem_free(m);
}

static void m3uw_expand_done(void *ctx)
{
	struct m3uw *m = ctx;
	m->track->cmd(m->trk, FMED_TRACK_WAKE);
	/* Note: refcount for m->cur is increased in FMED_QUE_EXPAND2 handler
	 to prevent it from being removed from the list.
	We should decrease it after FMED_QUE_LIST. */
}

static int m3uw_process(void *ctx, fmed_filt *d)
{
	struct m3uw *m = ctx;
	fmed_que_entry *e = m->cur;
	int r = 0;
	ffstr ss = {}, *s;

	while (0 != qu->cmdv(FMED_QUE_LIST, &e)) {

		uint t = core->cmd(FMED_FILETYPE, e->url.ptr);
		if (t == FMED_FT_DIR || t == FMED_FT_PLIST) {
			m->cur = e;
			void *trk = (void*)qu->cmdv(FMED_QUE_EXPAND2, e, &m3uw_expand_done, m);
			if (trk == NULL || trk == FMED_TRK_EFMT)
				continue;
			return FMED_RASYNC;
		}

		m3uwrite_entry m3e = {};

		int dur = (e->dur != 0) ? e->dur / 1000 : -1;
		m3e.duration_sec = dur;

		if (NULL == (s = qu->meta_find(e, FFSTR("artist"))))
			s = &ss;
		m3e.artist = *s;

		if (NULL == (s = qu->meta_find(e, FFSTR("title"))))
			s = &ss;
		m3e.title = *s;

		m3e.url = e->url;
		r = m3uwrite_process(&m->m3, &m3e);

		if (r != 0) {
			fmed_errlog(core, NULL, "m3u", "saving playlist to file: %d", r);
			return FMED_RERR;
		}
	}

	ffstr out = m3uwrite_fin(&m->m3);
	d->out = out.ptr;  d->outlen = out.len;
	return FMED_RLASTOUT;
}

static const fmed_filter m3u_output = { m3uw_open, m3uw_process, m3uw_close };
