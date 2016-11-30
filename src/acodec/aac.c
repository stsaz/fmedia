/** AAC input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/aac.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* aac_iface(const char *name);
static int aac_conf(const char *name, ffpars_ctx *ctx);
static int aac_sig(uint signo);
static void aac_destroy(void);
static const fmed_mod fmed_aac_mod = {
	&aac_iface, &aac_sig, &aac_destroy, &aac_conf
};

//DECODE
static void* aac_open(fmed_filt *d);
static void aac_close(void *ctx);
static int aac_decode(void *ctx, fmed_filt *d);
static const fmed_filter aac_input = {
	&aac_open, &aac_decode, &aac_close
};

//ENCODE CONFIG
static struct aac_out_conf_t {
	uint aot;
	uint qual;
	uint afterburner;
	uint bandwidth;
} aac_out_conf;

static const ffpars_arg aac_out_conf_args[] = {
	{ "profile",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, aot) },
	{ "quality",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, qual) },
	{ "afterburner",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, afterburner) },
	{ "bandwidth",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, bandwidth) },
};

//ENCODE
static int aac_out_config(ffpars_ctx *ctx);
static void* aac_out_create(fmed_filt *d);
static void aac_out_free(void *ctx);
static int aac_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter aac_output = {
	&aac_out_create, &aac_out_encode, &aac_out_free
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
	else if (!ffsz_cmp(name, "encode"))
		return &aac_output;
	return NULL;
}

static int aac_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return aac_out_config(ctx);
	return -1;
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
	uint sample_rate;
} aac_in;

static void* aac_open(fmed_filt *d)
{
	aac_in *a;
	if (NULL == (a = ffmem_tcalloc1(aac_in)))
		return NULL;

	a->aac.enc_delay = fmed_popval_def(d, "audio_enc_delay", 0);
	a->aac.end_padding = fmed_popval_def(d, "audio_end_padding", 0);
	a->aac.total_samples = d->audio.total;
	if (0 != ffaac_open(&a->aac, d->audio.fmt.channels, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffaac_open(): %s", ffaac_errstr(&a->aac));
		ffmem_free(a);
		return NULL;
	}
	d->datalen = 0;
	d->audio.fmt.format = a->aac.fmt.format;
	d->audio.fmt.ileaved = 1;
	a->sample_rate = d->audio.fmt.sample_rate;
	d->track->setvalstr(d->trk, "pcm_decoder", "AAC");
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
		uint64 seek = ffpcm_samples(d->audio.seek, a->sample_rate);
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


typedef struct aac_out {
	uint state;
	ffpcm fmt;
	ffaac_enc aac;
} aac_out;

static int aac_out_config(ffpars_ctx *ctx)
{
	aac_out_conf.aot = AAC_LC;
	aac_out_conf.qual = 256;
	aac_out_conf.afterburner = 1;
	aac_out_conf.bandwidth = 0;
	ffpars_setargs(ctx, &aac_out_conf, aac_out_conf_args, FFCNT(aac_out_conf_args));
	return 0;
}

static void* aac_out_create(fmed_filt *d)
{
	aac_out *a = ffmem_tcalloc1(aac_out);
	if (a == NULL)
		return NULL;
	return a;
}

static void aac_out_free(void *ctx)
{
	aac_out *a = ctx;
	ffaac_enc_close(&a->aac);
	ffmem_free(a);
}

static int aac_out_encode(void *ctx, fmed_filt *d)
{
	aac_out *a = ctx;
	int r;
	enum { W_CONV, W_CREATE, W_DATA };

	switch (a->state) {
	case W_CONV:
		d->audio.convfmt.format = FFPCM_16;
		d->audio.convfmt.ileaved = 1;
		a->state = W_CREATE;
		return FMED_RMORE;

	case W_CREATE:
		if (d->audio.convfmt.format != FFPCM_16LE || !d->audio.convfmt.ileaved) {
			errlog(core, d->trk, NULL, "unsupported input PCM format");
			return FMED_RERR;
		}

		ffpcm_fmtcopy(&a->fmt, &d->audio.convfmt);

		int qual;
		if (FMED_NULL == (qual = fmed_getval("aac-quality")))
			qual = aac_out_conf.qual;
		if (qual > 5 && qual < 8000)
			qual *= 1000;

		a->aac.info.aot = aac_out_conf.aot;
		a->aac.info.afterburner = aac_out_conf.afterburner;
		a->aac.info.bandwidth = aac_out_conf.bandwidth;

		if (0 != (r = ffaac_create(&a->aac, &a->fmt, qual))) {
			errlog(core, d->trk, NULL, "ffaac_create(): %s", ffaac_enc_errstr(&a->aac));
			return FMED_RERR;
		}

		fmed_setval("audio_enc_delay", a->aac.info.enc_delay);
		fmed_setval("audio_frame_samples", ffaac_enc_frame_samples(&a->aac));
		fmed_setval("audio_bitrate", ffaac_bitrate(&a->aac, qual));
		ffstr asc = ffaac_enc_conf(&a->aac);
		d->out = asc.ptr,  d->outlen = asc.len;
		a->state = W_DATA;
		return FMED_RDATA;
	}

	if (d->flags & FMED_FLAST)
		a->aac.fin = 1;

	a->aac.pcm = (void*)d->data,  a->aac.pcmlen = d->datalen;
	r = ffaac_encode(&a->aac);

	switch (r) {
	case FFAAC_RDONE:
		d->outlen = 0;
		return FMED_RDONE;

	case FFAAC_RMORE:
		return FMED_RMORE;

	case FFAAC_RDATA:
		break;

	case FFAAC_RERR:
		errlog(core, d->trk, NULL, "ffaac_encode(): %s", ffaac_enc_errstr(&a->aac));
		return FMED_RERR;
	}

	dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes"
		, (d->datalen - a->aac.pcmlen) / ffpcm_size1(&a->fmt), a->aac.datalen);
	d->data = (void*)a->aac.pcm,  d->datalen = a->aac.pcmlen;
	d->out = a->aac.data,  d->outlen = a->aac.datalen;
	return FMED_RDATA;
}
