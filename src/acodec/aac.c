/** AAC input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/aac.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* aac_iface(const char *name);
static int aac_sig(uint signo);
static void aac_destroy(void);
static const fmed_mod fmed_aac_mod = {
	&aac_iface, &aac_sig, &aac_destroy
};

//DECODE
static void* aac_open(fmed_filt *d);
static void aac_close(void *ctx);
static int aac_decode(void *ctx, fmed_filt *d);
static const fmed_filter aac_input = {
	&aac_open, &aac_decode, &aac_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_aac_mod;
}


static const void* aac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &aac_input;
	return NULL;
}

static int aac_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;
	}
	return 0;
}

static void aac_destroy(void)
{
}


typedef struct aac_in {
	ffaac aac;
} aac_in;

static void* aac_open(fmed_filt *d)
{
	aac_in *a;
	if (NULL == (a = ffmem_tcalloc1(aac_in)))
		return NULL;

	a->aac.enc_delay = fmed_popval("audio_enc_delay");
	a->aac.end_padding = fmed_popval("audio_end_padding");
	a->aac.total_samples = d->audio.total;
	if (0 != ffaac_open(&a->aac, d->audio.fmt.channels, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffaac_open(): %s", ffaac_errstr(&a->aac));
		ffmem_free(a);
		return NULL;
	}
	d->datalen = 0;
	d->audio.fmt.ileaved = 1;
	return a;
}

static void aac_close(void *ctx)
{
	aac_in *a = ctx;
	ffaac_close(&a->aac);
	ffmem_free(a);
}

static int aac_decode(void *ctx, fmed_filt *d)
{
	aac_in *a = ctx;

	if (d->input_info)
		return FMED_RDONE;

	if (d->flags & FMED_FFWD) {
		a->aac.data = d->data,  a->aac.datalen = d->datalen;
		d->datalen = 0;
		a->aac.cursample = d->audio.pos;
	}

	if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
		uint64 seek = ffpcm_samples(d->audio.seek, d->audio.fmt.sample_rate);
		ffaac_seek(&a->aac, seek);
		d->audio.seek = FMED_NULL;
	}

	int r;
	r = ffaac_decode(&a->aac);
	if (r == FFAAC_RERR) {
		errlog(core, d->trk, NULL, "ffaac_decode(): %s", ffaac_errstr(&a->aac));
		return FMED_RERR;

	} else if (r == FFAAC_RMORE) {
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;
	}

	dbglog(core, d->trk, NULL, "decoded %u samples (%U)"
		, a->aac.pcmlen / ffpcm_size1(&a->aac.fmt), ffaac_cursample(&a->aac));
	d->audio.pos = ffaac_cursample(&a->aac);

	d->out = (void*)a->aac.pcm,  d->outlen = a->aac.pcmlen;
	return FMED_RDATA;
}
