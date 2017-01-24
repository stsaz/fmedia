/** SoXr filter.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/soxr.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* soxr_mod_iface(const char *name);
static int soxr_mod_sig(uint signo);
static void soxr_mod_destroy(void);
static const fmed_mod soxr_mod = {
	&soxr_mod_iface, &soxr_mod_sig, &soxr_mod_destroy
};

//CONVERTER-SOXR
static void* soxr_open(fmed_filt *d);
static int soxr_conv(void *ctx, fmed_filt *d);
static void soxr_close(void *ctx);
static const fmed_filter fmed_soxr = {
	&soxr_open, &soxr_conv, &soxr_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &soxr_mod;
}


static const void* soxr_mod_iface(const char *name)
{
	if (!ffsz_cmp(name, "conv"))
		return &fmed_soxr;
	return NULL;
}

static int soxr_mod_sig(uint signo)
{
	return 0;
}

static void soxr_mod_destroy(void)
{
}


typedef struct soxr {
	uint state;
	ffsoxr soxr;
	ffpcm inpcm;
} soxr;

static void* soxr_open(fmed_filt *d)
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

static void log_pcmconv(const char *module, int r, const ffpcmex *in, const ffpcmex *out, void *trk)
{
	int f = FMED_LOG_DEBUG;
	const char *unsupp = "";
	if (r != 0) {
		f = FMED_LOG_ERR;
		unsupp = "unsupported ";
	}
	core->log(f, trk, module, "%sPCM conversion: %s/%u/%u/%s -> %s/%u/%u/%s"
		, unsupp
		, ffpcm_fmtstr(in->format), in->channels, in->sample_rate, (in->ileaved) ? "i" : "ni"
		, ffpcm_fmtstr(out->format), out->channels & FFPCM_CHMASK, out->sample_rate, (out->ileaved) ? "i" : "ni");
}

/*
This filter converts both format and sample rate.
Previous filter must deal with channel conversion.
*/
static int soxr_conv(void *ctx, fmed_filt *d)
{
	soxr *c = ctx;
	int val;
	ffpcmex inpcm, outpcm;

	switch (c->state) {
	case 0:
		inpcm = d->audio.convfmt_in;
		outpcm = d->audio.convfmt;

		// c->soxr.dither = 1;
		if (0 != (val = ffsoxr_create(&c->soxr, &inpcm, &outpcm))
			|| (core->loglev == FMED_LOG_DEBUG)) {
			log_pcmconv("soxr", val, &inpcm, &outpcm, d->trk);
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
		errlog(core, d->trk, "soxr", "ffsoxr_convert(): %s", ffsoxr_errstr(&c->soxr));
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
