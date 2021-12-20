/** WavPack input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wavpack.h>
#include <format/mmtag.h>
#include <FF/number.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* wvpk_iface(const char *name);
static int wvpk_sig(uint signo);
static void wvpk_destroy(void);
static const fmed_mod fmed_wvpk_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&wvpk_iface, &wvpk_sig, &wvpk_destroy
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_wvpk_mod;
}


static const fmed_filter wvpk_dec_iface;

static const void* wvpk_iface(const char *name)
{
	if (ffsz_eq(name, "decode"))
		return &wvpk_dec_iface;
	return NULL;
}

static int wvpk_sig(uint signo)
{
	return 0;
}

static void wvpk_destroy(void)
{
}


typedef struct wvpk_dec {
	ffwvpack_dec wv;
	uint frsize;
	uint sample_rate;
} wvpk_dec;

static void* wvpk_dec_create(fmed_filt *d)
{
	wvpk_dec *w = ffmem_new(wvpk_dec);
	ffwvpk_dec_open(&w->wv);
	return w;
}

static void wvpk_dec_free(void *ctx)
{
	wvpk_dec *w = ctx;
	ffwvpk_dec_close(&w->wv);
	ffmem_free(w);
}

static int wvpk_dec_decode(void *ctx, fmed_filt *d)
{
	wvpk_dec *w = ctx;

	if (d->audio.decoder_seek_msec != 0) {
		ffwvpk_dec_seek(&w->wv, ffpcm_samples(d->audio.decoder_seek_msec, w->sample_rate));
		d->audio.decoder_seek_msec = 0;
	}

	int r = ffwvpk_decode(&w->wv, &d->data_in, &d->data_out, d->audio.pos);

	switch (r) {
	case FFWVPK_RHDR: {
		const struct ffwvpk_info *info = ffwvpk_dec_info(&w->wv);
		dbglog(core, d->trk, "wvpk", "lossless:%u  compression:%u  MD5:%16xb"
			, (int)info->lossless
			, info->comp_level
			, info->md5);
		d->audio.decoder = "WavPack";
		d->audio.fmt.format = info->format;
		d->audio.fmt.channels = info->channels;
		d->audio.fmt.sample_rate = info->sample_rate;
		d->audio.fmt.ileaved = 1;
		w->sample_rate = info->sample_rate;
		d->audio.bitrate = ffpcm_brate(d->input.size, d->audio.total, info->sample_rate);
		d->datatype = "pcm";

		if (d->input_info)
			return FMED_RDONE;

		w->frsize = ffpcm_size(info->format, info->channels);
		return FMED_RMORE;
	}

	case FFWVPK_RDATA:
		break;

	case FFWVPK_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFWVPK_RERR:
		errlog(core, d->trk, "wvpk", "ffwvpk_decode(): %s", ffwvpk_dec_error(&w->wv));
		return FMED_RERR;

	default:
		FF_ASSERT(0);
		return FMED_RERR;
	}

	dbglog(core, d->trk, "wvpk", "decoded %L samples (%U)"
		, (size_t)d->data_out.len / w->frsize, (int64)w->wv.samp_idx);
	d->audio.pos = w->wv.samp_idx;
	return FMED_RDATA;
}

static const fmed_filter wvpk_dec_iface = {
	wvpk_dec_create, wvpk_dec_decode, wvpk_dec_free
};
