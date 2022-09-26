/** fmedia: --until filter
2015,2022 Simon Zolin */

#include <fmedia.h>
#include <afilter/pcm.h>

struct until {
	uint64 until;
	uint64 total;
	uint sampsize;
};

static void* until_open(fmed_filt *d)
{
	int64 val;
	struct until *u;

	if (FMED_NULL == (val = d->audio.until))
		return FMED_FILT_SKIP;

	if (NULL == (u = ffmem_new(struct until)))
		return NULL;
	if (val > 0)
		u->until = ffpcm_samples(val, d->audio.fmt.sample_rate);
	else
		u->until = -val * d->audio.fmt.sample_rate / 75;

	u->sampsize = ffpcm_size(d->audio.fmt.format, d->audio.fmt.channels);

	if ((int64)d->audio.total != FMED_NULL)
		d->audio.total = u->until;
	return u;
}

static void until_close(void *ctx)
{
	struct until *u = ctx;
	ffmem_free(u);
}

static int until_process(void *ctx, fmed_filt *d)
{
	struct until *u = ctx;
	uint samps;
	uint64 pos;

	d->out = d->data;
	d->outlen = d->datalen;

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;

	if (FMED_NULL == (int64)(pos = d->audio.pos)) {
		pos = u->total;
		u->total += d->datalen / u->sampsize;
	}

	if (d->stream_copy) {
		if (pos >= u->until) {
			dbglog(core, d->trk, "until", "reached sample #%U", u->until);
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
		d->datalen = 0;
		return FMED_ROK;
	}

	samps = d->datalen / u->sampsize;
	dbglog(core, d->trk, "until", "at %U..%U", pos, pos + samps);
	d->datalen = 0;
	if (pos + samps >= u->until) {
		dbglog(core, d->trk, "until", "reached sample #%U", u->until);
		d->outlen = (u->until > pos) ? (u->until - pos) * u->sampsize : 0;
		return FMED_RLASTOUT;
	}

	return FMED_ROK;
}

static const fmed_filter fmed_sndmod_until = { until_open, until_process, until_close };
