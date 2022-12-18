/** fmedia: FLAC decode
2018, Simon Zolin */

#include <acodec/alib3-bridge/flac.h>

struct flac_dec {
	ffflac_dec fl;
	ffpcmex fmt;
};

static void flac_dec_free(void *ctx)
{
	struct flac_dec *f = ctx;
	ffflac_dec_close(&f->fl);
	ffmem_free(f);
}

static void* flac_dec_create(fmed_track_info *d)
{
	int r;
	struct flac_dec *f = ffmem_new(struct flac_dec);

	struct flac_info info;
	info.minblock = d->flac_minblock;
	info.maxblock = d->flac_maxblock;
	info.bits = ffpcm_bits(d->audio.fmt.format);
	info.channels = d->audio.fmt.channels;
	info.sample_rate = d->audio.fmt.sample_rate;
	f->fmt = d->audio.fmt;
	if (0 != (r = ffflac_dec_open(&f->fl, &info))) {
		errlog1(d->trk, "ffflac_dec_open(): %s", ffflac_dec_errstr(&f->fl));
		flac_dec_free(f);
		return NULL;
	}

	d->datatype = "pcm";
	return f;
}

static int flac_dec_decode(void *ctx, fmed_track_info *d)
{
	enum { I_HDR, I_DATA };
	struct flac_dec *f = ctx;
	int r;

	if (d->flags & FMED_FLAST) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		ffflac_dec_seek(&f->fl, ffpcm_samples(d->audio.seek, f->fmt.sample_rate));
	}
	ffflac_dec_input(&f->fl, &d->data_in, d->flac_samples, d->audio.pos);
	d->data_in.len = 0;

	r = ffflac_decode(&f->fl);
	switch (r) {
	case FFFLAC_RDATA:
		break;

	case FFFLAC_RWARN:
		warnlog1(d->trk, "ffflac_decode(): %s"
			, ffflac_dec_errstr(&f->fl));
		return FMED_RMORE;
	}

	d->audio.pos = ffflac_dec_cursample(&f->fl);
	d->outlen = ffflac_dec_output(&f->fl, &d->outni);
	dbglog1(d->trk, "decoded %L samples (%U)"
		, d->outlen / ffpcm_size1(&f->fmt), ffflac_dec_cursample(&f->fl));
	return FMED_ROK;
}

const fmed_filter fmed_flac_dec = { flac_dec_create, flac_dec_decode, flac_dec_free };
