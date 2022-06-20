/** AAC input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <acodec/alib3-bridge/aac.h>

const fmed_core *core;

#include <acodec/aac-enc.h>

//FMEDIA MODULE
static const void* aac_iface(const char *name);
static int aac_conf(const char *name, fmed_conf_ctx *ctx);
static int aac_sig(uint signo);
static void aac_destroy(void);
static const fmed_mod fmed_aac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&aac_iface, &aac_sig, &aac_destroy, &aac_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_aac_mod;
}

static const fmed_filter aac_input;
static const void* aac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &aac_input;
	else if (!ffsz_cmp(name, "encode"))
		return &aac_output;
	return NULL;
}

static int aac_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return aac_out_config(ctx);
	return -1;
}

static int aac_sig(uint signo)
{
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

enum { DETECT_FRAMES = 32, }; //# of frames to detect real audio format

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
	d->audio.decoder = "AAC";
	return a;
}

static void aac_close(void *ctx)
{
	aac_in *a = ctx;
	ffaac_close(&a->aac);
	ffarr_free(&a->cache);
	ffmem_free(a);
}

/** Dynamic mean value with weight. */
#define ffint_mean_dyn(mean, weight, add) \
	(((mean) * (weight) + (add)) / ((weight) + 1))

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
	uint fr_len = 0;
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
		warnlog(core, d->trk, NULL, "ffaac_decode(): (%xu) %s", a->aac.err, ffaac_errstr(&a->aac));
		return FMED_RMORE;

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
		fr_len = a->aac.pcmlen;
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

		d->audio.pos = ffaac_cursample(&a->aac) + fr_len / ffpcm_size1(&a->aac.fmt) - a->cache.len / ffpcm_size1(&a->aac.fmt);
		d->audio.pos = ffmax((ffint64)d->audio.pos, 0);
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

static const fmed_filter aac_input = { aac_open, aac_decode, aac_close };
