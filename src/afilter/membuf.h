/** fmedia: afilter: temporary memory buffer
2019 Simon Zolin */

#include <fmedia.h>

struct membuf {
	ffringbuf buf;
	size_t size;
};

static void* membuf_open(fmed_filt *d)
{
	if (!d->audio.fmt.ileaved) {
		errlog(core, d->trk, "afilter.membuf", "non-interleaved audio format isn't supported");
		return NULL;
	}

	struct membuf *m = ffmem_new(struct membuf);
	if (m == NULL)
		return NULL;

	size_t size = ffpcm_bytes(&d->audio.fmt, d->a_prebuffer);
	m->size = size;
	size = ff_align_power2(size + 1);
	void *p = ffmem_alloc(size);
	if (p == NULL) {
		ffmem_free(m);
		return NULL;
	}
	ffringbuf_init(&m->buf, p, size);
	return m;
}

static void membuf_close(void *ctx)
{
	struct membuf *m = ctx;
	ffmem_free(ffringbuf_data(&m->buf));
}

static int membuf_write(void *ctx, fmed_filt *d)
{
	struct membuf *m = ctx;

	if (d->save_trk) {
		ffstr s;
		ffringbuf_readptr(&m->buf, &s, m->size);
		d->out = s.ptr,  d->outlen = s.len;
		return (s.len == 0) ? FMED_RDONE : FMED_RDATA;
	}

	if (d->flags & FMED_FSTOP)
		return FMED_RFIN;

	ffringbuf_overwrite(&m->buf, d->data, d->datalen);
	return FMED_RMORE;
}

const fmed_filter sndmod_membuf = { membuf_open, membuf_write, membuf_close };
