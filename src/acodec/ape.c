/** APE input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <acodec/alib3-bridge/ape.h>
#include <format/mmtag.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* ape_iface(const char *name);
static int ape_sig(uint signo);
static void ape_destroy(void);
static const fmed_mod fmed_ape_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&ape_iface, &ape_sig, &ape_destroy
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_ape_mod;
}

static const fmed_filter ape_dec;
static const void* ape_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &ape_dec;
	return NULL;
}

static int ape_sig(uint signo)
{
	return 0;
}

static void ape_destroy(void)
{
}


typedef struct ape {
	ffape ap;
	uint state;
	uint sample_rate;
} ape;

static void* ape_dec_create(fmed_filt *d)
{
	ape *a = ffmem_new(ape);
	ffape_open(&a->ap);
	return a;
}

static void ape_dec_free(void *ctx)
{
	ape *a = ctx;
	ffape_close(&a->ap);
	ffmem_free(a);
}

static int ape_dec_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	ape *a = ctx;
	int r;

	ffuint block_samples = fmed_getval("ape_block_samples");
	ffuint align4 = fmed_getval("ape_align4");

	if (d->audio.decoder_seek_msec != 0) {
		ffape_seek(&a->ap, ffpcm_samples(d->audio.decoder_seek_msec, a->sample_rate));
		d->audio.decoder_seek_msec = 0;
	}

	r = ffape_decode(&a->ap, &d->data_in, &d->data_out, d->audio.pos, block_samples, align4);
	switch (r) {
	case FFAPE_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFAPE_RHDR: {
		const ffape_info *info = &a->ap.info;
		d->audio.decoder = "APE";
		d->audio.fmt.format = info->fmt.format;
		d->audio.fmt.channels = info->fmt.channels;
		d->audio.fmt.sample_rate = info->fmt.sample_rate;
		d->audio.fmt.ileaved = 1;
		d->datatype = "pcm";
		d->audio.bitrate = ffpcm_brate(d->input.size, d->audio.total, info->fmt.sample_rate);
		d->audio.total = ffape_totalsamples(&a->ap);
		a->sample_rate = info->fmt.sample_rate;

		if (d->input_info) {
			d->data_out.len = 0;
			return FMED_RDATA;
		}
		return FMED_RMORE;
	}

	case FFAPE_RDATA:
		goto data;

	case FFAPE_RERR:
		errlog(core, d->trk, "ape", "ffape_decode(): %s", ffape_errstr(&a->ap));
		return FMED_RERR;

	default:
		FF_ASSERT(0);
		return FMED_RERR;
	}

data:
	dbglog(core, d->trk, "ape", "decoded %L samples (%U)"
		, d->data_out.len / ffpcm_size1(&a->ap.info.fmt), ffape_cursample(&a->ap));
	d->audio.pos = ffape_cursample(&a->ap);
	return FMED_RDATA;
}

static const fmed_filter ape_dec = { ape_dec_create, ape_dec_decode, ape_dec_free };
