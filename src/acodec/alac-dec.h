/** fmedia: ALAC input
2016, Simon Zolin */

#include <acodec/alib3-bridge/alac.h>

typedef struct alac_in {
	ffalac alac;
} alac_in;

static void* alac_open(fmed_track_info *d)
{
	alac_in *a;
	if (NULL == (a = ffmem_new(alac_in)))
		return NULL;

	if (0 != ffalac_open(&a->alac, d->data, d->datalen)) {
		errlog1(d->trk, "ffalac_open(): %s", ffalac_errstr(&a->alac));
		ffmem_free(a);
		return NULL;
	}
	a->alac.total_samples = d->audio.total;
	d->datalen = 0;

	if (a->alac.bitrate != 0)
		d->audio.bitrate = a->alac.bitrate;
	ffpcm_fmtcopy(&d->audio.fmt, &a->alac.fmt);
	d->audio.fmt.ileaved = 1;
	d->audio.decoder = "ALAC";
	d->datatype = "pcm";
	return a;
}

static void alac_close(void *ctx)
{
	alac_in *a = ctx;
	ffalac_close(&a->alac);
	ffmem_free(a);
}

static int alac_in_decode(void *ctx, fmed_track_info *d)
{
	alac_in *a = ctx;

	if (d->flags & FMED_FFWD) {
		a->alac.data = d->data,  a->alac.datalen = d->datalen;
		d->datalen = 0;
		a->alac.cursample = d->audio.pos;
		if ((int64)d->audio.seek != FMED_NULL) {
			uint64 seek = ffpcm_samples(d->audio.seek, a->alac.fmt.sample_rate);
			ffalac_seek(&a->alac, seek);
		}
	}

	int r;
	r = ffalac_decode(&a->alac);
	if (r == FFALAC_RERR) {
		errlog1(d->trk, "ffalac_decode(): %s", ffalac_errstr(&a->alac));
		return FMED_RERR;

	} else if (r == FFALAC_RMORE) {
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;
	}

	dbglog1(d->trk, "decoded %u samples (%U)"
		, a->alac.pcmlen / ffpcm_size1(&a->alac.fmt), ffalac_cursample(&a->alac));
	d->audio.pos = ffalac_cursample(&a->alac);

	d->out = a->alac.pcm,  d->outlen = a->alac.pcmlen;
	return FMED_RDATA;
}

const fmed_filter alac_input = { alac_open, alac_in_decode, alac_close };
