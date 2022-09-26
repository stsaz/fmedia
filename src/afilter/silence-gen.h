/** fmedia: silence generator filter
2015,2022 Simon Zolin */

#include <fmedia.h>

#define SILGEN_BUF_MSEC 100

struct silgen {
	uint state;
	void *buf;
	size_t cap;
};

static void* silgen_open(fmed_filt *d)
{
	struct silgen *c;
	if (NULL == (c = ffmem_new(struct silgen)))
		return NULL;
	return c;
}

static void silgen_close(void *ctx)
{
	struct silgen *c = ctx;
	ffmem_free(c->buf);
	ffmem_free(c);
}

static int silgen_process(void *ctx, fmed_filt *d)
{
	struct silgen *c = ctx;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	switch (c->state) {

	case 0:
		d->audio.convfmt = d->audio.fmt;
		d->datatype = "pcm";
		c->state = 1;
		return FMED_RDATA;

	case 1:
		c->cap = ffpcm_bytes(&d->audio.convfmt, SILGEN_BUF_MSEC);
		if (NULL == (c->buf = ffmem_alloc(c->cap)))
			return FMED_RSYSERR;
		ffmem_zero(c->buf, c->cap);
		c->state = 2;
		// fall through

	case 2:
		break;
	}

	d->out = c->buf,  d->outlen = c->cap;
	return FMED_RDATA;
}

static const fmed_filter sndmod_silgen = { silgen_open, silgen_process, silgen_close };
