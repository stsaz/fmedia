/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>


static const fmed_core *core;

typedef struct sndmod_conv {
	uint state;
	ffpcmex inpcm
		, outpcm;
	ffstr3 buf;

	//for handling large non-ileaved input data:
	void **niarr;
	uint nioff;
} sndmod_conv;

enum {
	conf_bufsize = 64 * 1024
};

//FMEDIA MODULE
static const fmed_filter* sndmod_iface(const char *name);
static int sndmod_sig(uint signo);
static void sndmod_destroy(void);
static const fmed_mod fmed_sndmod_mod = {
	&sndmod_iface, &sndmod_sig, &sndmod_destroy
};

//CONVERTER
static void* sndmod_conv_open(fmed_filt *d);
static int sndmod_conv_process(void *ctx, fmed_filt *d);
static void sndmod_conv_close(void *ctx);
static const fmed_filter fmed_sndmod_conv = {
	&sndmod_conv_open, &sndmod_conv_process, &sndmod_conv_close
};

//GAIN
static void* sndmod_gain_open(fmed_filt *d);
static int sndmod_gain_process(void *ctx, fmed_filt *d);
static void sndmod_gain_close(void *ctx);
static const fmed_filter fmed_sndmod_gain = {
	&sndmod_gain_open, &sndmod_gain_process, &sndmod_gain_close
};


const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_sndmod_mod;
}


static const fmed_filter* sndmod_iface(const char *name)
{
	if (!ffsz_cmp(name, "conv"))
		return &fmed_sndmod_conv;
	else if (!ffsz_cmp(name, "gain"))
		return &fmed_sndmod_gain;
	return NULL;
}

static int sndmod_sig(uint signo)
{
	return 0;
}

static void sndmod_destroy(void)
{
}


static void* sndmod_conv_open(fmed_filt *d)
{
	sndmod_conv *c = ffmem_tcalloc1(sndmod_conv);

	if (c == NULL)
		return NULL;

	c->inpcm.format = (int)fmed_getval("pcm_format");
	c->inpcm.sample_rate = (int)fmed_getval("pcm_sample_rate");
	c->inpcm.channels = (int)fmed_getval("pcm_channels");
	if (1 == fmed_getval("pcm_ileaved"))
		c->inpcm.ileaved = 1;
	c->outpcm = c->inpcm;

	if (NULL == ffarr_alloc(&c->buf, conf_bufsize)) {
		ffmem_free(c);
		return NULL;
	}
	c->buf.len = conf_bufsize;

	return c;
}

static void sndmod_conv_close(void *ctx)
{
	sndmod_conv *c = ctx;
	ffmem_safefree(c->niarr);
	ffarr_free(&c->buf);
	ffmem_free(c);
}

enum { CONV_CONF, CONV_CHK, CONV_DATA };

static int sndmod_conv_prepare(sndmod_conv *c, fmed_filt *d)
{
	int il, fmt;

	fmt = (int)fmed_popval("conv_pcm_format");
	il = (int)fmed_popval("conv_pcm_ileaved");

	if (fmt != FMED_NULL) {
		c->outpcm.format = fmt;
		fmed_setval("pcm_format", c->outpcm.format);
	}

	if (il != FMED_NULL) {
		c->outpcm.ileaved = il;
		fmed_setval("pcm_ileaved", c->outpcm.ileaved);
	}

	if ((c->state == CONV_CONF && (fmt == FMED_NULL || il == FMED_NULL))
		|| (c->state == CONV_CHK && !ffmemcmp(&c->outpcm, &c->inpcm, sizeof(ffpcmex)))) {

		if (c->state == CONV_CHK) {
			d->out = d->data;
			d->outlen = d->datalen;
			return FMED_RDONE; //the second call of the module - no conversion is needed
		}

		d->outlen = 0;
		c->state = CONV_CHK;
		return FMED_ROK;
	}

	dbglog(core, d->trk, "conv", "PCM convertion: %u/%u/%u/%s -> %u/%u/%u/%s"
		, c->inpcm.format, c->inpcm.channels, c->inpcm.sample_rate, (c->inpcm.ileaved) ? "i" : "ni"
		, c->outpcm.format, c->outpcm.channels, c->outpcm.sample_rate, (c->outpcm.ileaved) ? "i" : "ni");

	if (!c->outpcm.ileaved) {
		void **ni;
		char *data;
		size_t cap = c->buf.cap;
		uint i;
		if (NULL == ffarr_realloc(&c->buf, sizeof(void*) * c->outpcm.channels + c->buf.cap)) {
			return FMED_RERR;
		}
		ni = (void**)c->buf.ptr;
		data = c->buf.ptr + sizeof(void*) * c->outpcm.channels;
		for (i = 0;  i < c->outpcm.channels;  i++) {
			ni[i] = data + cap * i / c->outpcm.channels;
		}
	}

	if (!c->inpcm.ileaved) {
		if (NULL == (c->niarr = ffmem_alloc(c->inpcm.channels)))
			return FMED_RERR;
	}

	c->buf.len /= ffpcm_size1(&c->outpcm);
	c->state = CONV_DATA;
	return FMED_ROK;
}

static int sndmod_conv_process(void *ctx, fmed_filt *d)
{
	sndmod_conv *c = ctx;
	uint samples, i;
	int r;
	const void *in;

	switch (c->state) {
	case CONV_CONF:
	case CONV_CHK:
		r = sndmod_conv_prepare(c, d);
		if (c->state != 2)
			return r;
		break;

	case CONV_DATA:
		break;
	}

	samples = (uint)ffmin(d->datalen / ffpcm_size1(&c->inpcm), c->buf.len);

	in = d->data;
	if (!c->inpcm.ileaved) {
		if (c->nioff == 0)
			in = d->datani;
		else {
			for (i = 0;  i < c->inpcm.channels;  i++) {
				c->niarr[i] = (byte*)d->datani[i] + c->nioff;
			}
			in = c->niarr;
		}
	}

	if (0 != ffpcm_convert(&c->outpcm, c->buf.ptr, &c->inpcm, in, samples)) {
		errlog(core, d->trk, "conv", "unsupported PCM conversion: %u/%u/%u/%s -> %u/%u/%u/%s"
			, c->inpcm.format, c->inpcm.channels, c->inpcm.sample_rate, (c->inpcm.ileaved) ? "i" : "ni"
			, c->outpcm.format, c->outpcm.channels, c->outpcm.sample_rate, (c->outpcm.ileaved) ? "i" : "ni");
		return FMED_RERR;
	}

	d->out = c->buf.ptr;
	d->outlen = samples * ffpcm_size1(&c->outpcm);
	d->datalen -= samples * ffpcm_size1(&c->inpcm);

	if (c->inpcm.ileaved)
		d->data += samples * ffpcm_size1(&c->inpcm);
	else
		c->nioff = (d->datalen != 0) ? c->nioff + samples : 0;

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
	return FMED_ROK;
}


static void* sndmod_gain_open(fmed_filt *d)
{
	ffpcmex *pcm = ffmem_tcalloc1(ffpcmex);
	if (pcm == NULL)
		return NULL;
	pcm->format = (int)fmed_getval("pcm_format");
	pcm->channels = (int)fmed_getval("pcm_channels");
	pcm->ileaved = (int)fmed_getval("pcm_ileaved");
	return pcm;
}

static void sndmod_gain_close(void *ctx)
{
	ffpcmex *pcm = ctx;
	ffmem_free(pcm);
}

static int sndmod_gain_process(void *ctx, fmed_filt *d)
{
	ffpcmex *pcm = ctx;
	int db = (int)fmed_getval("gain");
	if (db != FMED_NULL)
		ffpcm_gain(pcm, ffpcm_db2gain((double)db / 100), d->data, (void*)d->data, d->datalen / ffpcm_size1(pcm));

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}
