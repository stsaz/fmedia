/** fmedia: afilter: soxr sample rate convertor
2023, Simon Zolin */

#include <fmedia.h>
#include <afilter/soxr.h>

typedef struct soxr {
	uint state;
	ffsoxr soxr;
	ffpcmex inpcm, outpcm;
} soxr;

static void* soxr_open(fmed_track_info *d)
{
	soxr *c = ffmem_tcalloc1(soxr);
	if (c == NULL)
		return NULL;
	ffsoxr_init(&c->soxr);
	return c;
}

static void soxr_close(void *ctx)
{
	soxr *c = ctx;
	ffsoxr_destroy(&c->soxr);
	ffmem_free(c);
}

static ssize_t soxr_cmd(void *ctx, uint cmd, ...)
{
	soxr *c = ctx;
	va_list va;
	va_start(va, cmd);
	ssize_t r = -1;

	switch (cmd) {
	case 0: {
		const struct fmed_aconv *conf = va_arg(va, void*);
		c->inpcm = conf->in;
		c->outpcm = conf->out;
		r = 0;
		break;
	}
	}

	va_end(va);
	return r;
}

static void log_pcmconv(int r, const ffpcmex *in, const ffpcmex *out, void *trk)
{
	int f = FMED_LOG_DEBUG;
	const char *unsupp = "";
	if (r != 0) {
		f = FMED_LOG_ERR;
		unsupp = "unsupported ";
	}
	core->log(f, trk, NULL, "%sPCM conversion: %s/%u/%u/%s -> %s/%u/%u/%s"
		, unsupp
		, ffpcm_fmtstr(in->format), in->sample_rate, in->channels, (in->ileaved) ? "i" : "ni"
		, ffpcm_fmtstr(out->format), out->sample_rate, (out->channels & FFPCM_CHMASK), (out->ileaved) ? "i" : "ni");
}

/*
This filter converts both format and sample rate.
Previous filter must deal with channel conversion.
*/
static int soxr_conv(void *ctx, fmed_track_info *d)
{
	soxr *c = ctx;
	int val;
	ffpcmex inpcm, outpcm;

	switch (c->state) {
	case 0:
		inpcm = c->inpcm;
		outpcm = c->outpcm;

		// c->soxr.dither = 1;
		if (0 != (val = ffsoxr_create(&c->soxr, &inpcm, &outpcm))
			|| (core->loglev == FMED_LOG_DEBUG)) {
			log_pcmconv(val, &inpcm, &outpcm, d->trk);
			if (val != 0)
				return FMED_RERR;
		}

		c->state = 3;
		break;

	case 3:
		break;
	}

	c->soxr.in_i = d->data;
	c->soxr.inlen = d->datalen;
	if (d->flags & FMED_FLAST)
		c->soxr.fin = 1;
	if (0 != ffsoxr_convert(&c->soxr)) {
		errlog1(d->trk, "ffsoxr_convert(): %s", ffsoxr_errstr(&c->soxr));
		return FMED_RERR;
	}

	d->out = c->soxr.out;
	d->outlen = c->soxr.outlen;

	if (c->soxr.outlen == 0) {
		if (d->flags & FMED_FLAST)
			return FMED_RDONE;
	}

	d->data = c->soxr.in_i;
	d->datalen = c->soxr.inlen;
	return FMED_ROK;
}

const struct fmed_filter2 fmed_soxr = { soxr_open, soxr_conv, soxr_close, soxr_cmd };
