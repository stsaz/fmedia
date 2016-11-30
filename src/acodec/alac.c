/** ALAC input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/alac.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* alac_iface(const char *name);
static int alac_sig(uint signo);
static void alac_destroy(void);
static const fmed_mod fmed_alac_mod = {
	&alac_iface, &alac_sig, &alac_destroy
};

//DECODE
static void* alac_open(fmed_filt *d);
static void alac_close(void *ctx);
static int alac_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter alac_input = {
	&alac_open, &alac_in_decode, &alac_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_alac_mod;
}


static const void* alac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &alac_input;
	return NULL;
}

static int alac_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;
	}
	return 0;
}

static void alac_destroy(void)
{
}


typedef struct alac_in {
	ffalac alac;
} alac_in;

static void* alac_open(fmed_filt *d)
{
	alac_in *a;
	if (NULL == (a = ffmem_tcalloc1(alac_in)))
		return NULL;

	if (0 != ffalac_open(&a->alac, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffalac_open(): %s", ffalac_errstr(&a->alac));
		ffmem_free(a);
		return NULL;
	}
	a->alac.total_samples = d->audio.total;
	d->datalen = 0;

	if (0 != memcmp(&a->alac.fmt, &d->audio.fmt, sizeof(a->alac.fmt)))
		warnlog(core, d->trk, NULL, "audio format doesn't match with format from MP4");

	if (a->alac.bitrate != 0)
		d->audio.bitrate = a->alac.bitrate;
	d->audio.fmt.ileaved = 1;
	d->track->setvalstr(d->trk, "pcm_decoder", "ALAC");
	return a;
}

static void alac_close(void *ctx)
{
	alac_in *a = ctx;
	ffalac_close(&a->alac);
	ffmem_free(a);
}

static int alac_in_decode(void *ctx, fmed_filt *d)
{
	alac_in *a = ctx;

	if (d->input_info)
		return FMED_RDONE;

	if (d->flags & FMED_FFWD) {
		a->alac.data = d->data,  a->alac.datalen = d->datalen;
		d->datalen = 0;
		a->alac.cursample = d->audio.pos;
	}

	if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
		uint64 seek = ffpcm_samples(d->audio.seek, d->audio.fmt.sample_rate);
		ffalac_seek(&a->alac, seek);
		d->audio.seek = FMED_NULL;
	}

	int r;
	r = ffalac_decode(&a->alac);
	if (r == FFALAC_RERR) {
		errlog(core, d->trk, NULL, "ffalac_decode(): %s", ffalac_errstr(&a->alac));
		return FMED_RERR;

	} else if (r == FFALAC_RMORE) {
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;
	}

	dbglog(core, d->trk, NULL, "decoded %u samples (%U)"
		, a->alac.pcmlen / ffpcm_size1(&a->alac.fmt), ffalac_cursample(&a->alac));
	d->audio.pos = ffalac_cursample(&a->alac);

	d->out = a->alac.pcm,  d->outlen = a->alac.pcmlen;
	return FMED_RDATA;
}
