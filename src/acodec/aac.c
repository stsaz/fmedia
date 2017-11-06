/** AAC input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/aac.h>


const fmed_core *core;

//FMEDIA MODULE
static const void* aac_iface(const char *name);
static int aac_conf(const char *name, ffpars_ctx *ctx);
static int aac_sig(uint signo);
static void aac_destroy(void);
static const fmed_mod fmed_aac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&aac_iface, &aac_sig, &aac_destroy, &aac_conf
};

extern const fmed_filter aac_adts_input;

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
	else if (!ffsz_cmp(name, "in"))
		return &aac_adts_input;
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
	uint state;
	ffarr cache;
	uint sample_rate;
	uint frnum;
	uint br;
} aac_in;

enum { DETECT_FRAMES = 16, }; //# of frames to detect real audio format

static void* aac_open(fmed_filt *d)
{
	aac_in *a;
	if (NULL == (a = ffmem_tcalloc1(aac_in)))
		return NULL;

	a->aac.enc_delay = fmed_popval_def(d, "audio_enc_delay", 0);
	a->aac.end_padding = fmed_popval_def(d, "audio_end_padding", 0);
	a->aac.total_samples = d->audio.total;
	a->aac.contr_samprate = d->audio.fmt.sample_rate;
	if (0 != ffaac_open(&a->aac, d->audio.fmt.channels, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffaac_open(): %s", ffaac_errstr(&a->aac));
		ffmem_free(a);
		return NULL;
	}
	d->datalen = 0;
	d->audio.fmt.format = a->aac.fmt.format;
	d->audio.fmt.ileaved = 1;
	a->sample_rate = d->audio.fmt.sample_rate;
	d->datatype = "pcm";
	return a;
}

static void aac_close(void *ctx)
{
	aac_in *a = ctx;
	ffaac_close(&a->aac);
	ffarr_free(&a->cache);
	ffmem_free(a);
}

/*
AAC audio format detection:
. gather decoded frames in cache
. when decoder notifies about changed audio format:
 . set new audio format
 . clear cached data
. when the needed number of frames has been processed, start returning data
If format changes again in the future, the change won't be handled.
*/
static int aac_decode(void *ctx, fmed_filt *d)
{
	aac_in *a = ctx;
	int r;
	enum { R_CACHE, R_CACHE_DATA, R_CACHE_DONE, R_PASS };

	if (d->input_info)
		return FMED_RDONE;

	if (d->flags & FMED_FFWD) {
		ffaac_input(&a->aac, d->data, d->datalen, d->audio.pos);
		d->datalen = 0;
	}

	if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
		uint64 seek = ffpcm_samples(d->audio.seek, a->sample_rate);
		ffaac_seek(&a->aac, seek);
		d->audio.seek = FMED_NULL;
	}

	for (;;) {

	r = ffaac_decode(&a->aac);
	if (r == FFAAC_RERR) {
		errlog(core, d->trk, NULL, "ffaac_decode(): %s", ffaac_errstr(&a->aac));
		return FMED_RERR;

	} else if (r == FFAAC_RMORE) {
		if (!(d->flags & FMED_FLAST))
			return FMED_RMORE;
		else if (a->cache.len == 0) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		a->state = R_CACHE_DATA;

	} else {
		dbglog(core, d->trk, NULL, "decoded %u samples (%U)"
			, a->aac.pcmlen / ffpcm_size1(&a->aac.fmt), ffaac_cursample(&a->aac));
	}

	switch (a->state) {
	case R_CACHE:
		if (r == FFAAC_RDATA_NEWFMT) {
			ffpcmex *fmt = &d->audio.fmt;
			fdkaac_info *inf = &a->aac.info;
			dbglog(core, d->trk, NULL, "overriding audio configuration: %u/%u -> %u/%u"
				, fmt->sample_rate, fmt->channels
				, inf->rate, inf->channels);
			if (fmt->sample_rate != inf->rate) {
				if (d->audio.total != 0) {
					d->audio.total *= a->aac.rate_mul;
					a->aac.total_samples = d->audio.total;
				}
			}
			a->sample_rate = fmt->sample_rate = inf->rate;
			fmt->channels = inf->channels;
			a->cache.len = 0;
		}
		a->br = ffint_mean_dyn(a->br, a->frnum, a->aac.info.bitrate);
		if (NULL == ffarr_append(&a->cache, a->aac.pcm, a->aac.pcmlen))
			return FMED_RSYSERR;
		if (++a->frnum != DETECT_FRAMES)
			continue;
		//fall through

	case R_CACHE_DATA:
		d->audio.bitrate = a->br;
		if (d->audio.total == 0 && (int64)d->input.size != FMED_NULL)
			d->audio.total = d->input.size * 8 * a->sample_rate / a->br;
		switch (a->aac.info.aot) {
		case AAC_LC:
			d->audio.decoder = "AAC-LC"; break;
		case AAC_HE:
			d->audio.decoder = "HE-AAC"; break;
		case AAC_HEV2:
			d->audio.decoder = "HE-AACv2"; break;
		}

		d->audio.pos = ffaac_cursample(&a->aac);
		d->out = (void*)a->cache.ptr,  d->outlen = a->cache.len;
		a->cache.len = 0;
		a->state = R_CACHE_DONE;
		return FMED_RDATA;

	case R_CACHE_DONE:
		ffarr_free(&a->cache);
		a->state = R_PASS;
		break;
	}

	if (a->state == R_PASS)
		break;

	}

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
		d->datatype = "AAC";

		int qual = (d->aac.quality != -1) ? d->aac.quality : (int)aac_out_conf.qual;
		if (qual > 5 && qual < 8000)
			qual *= 1000;

		a->aac.info.aot = aac_out_conf.aot;
		a->aac.info.afterburner = aac_out_conf.afterburner;
		a->aac.info.bandwidth = (d->aac.bandwidth != -1) ? d->aac.bandwidth : (int)aac_out_conf.bandwidth;

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
