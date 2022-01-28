/** fmedia: auto attenuator
2020, Simon Zolin */

#include <fmedia.h>

extern const fmed_core *core;
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, "auto-attenuator", __VA_ARGS__)

struct aa_ctx {
	uint state;
	ffpcmex orig_convfmt;
	double track_gain;
	double track_ceiling;
	double ceiling;
	int user_gain_int;
	double user_gain;
};

static void* aa_open(fmed_filt *d)
{
	if (d->audio.auto_attenuate_ceiling == 0.0)
		return FMED_FILT_SKIP;

	struct aa_ctx *aa = ffmem_new(struct aa_ctx);
	aa->track_gain = 1.0;
	aa->ceiling = ffpcm_db2gain(d->audio.auto_attenuate_ceiling);
	aa->track_ceiling = aa->ceiling;
	aa->user_gain = 1.0;
	return aa;
}

static void aa_close(void *ctx)
{
	struct aa_ctx *aa = ctx;
	ffmem_free(aa);
}

static int aa_process(void *ctx, fmed_filt *d)
{
	struct aa_ctx *aa = ctx;

	if (d->flags & FMED_FSTOP) {
		return FMED_RDONE;
	}

	switch (aa->state) {
	case 0:
		aa->orig_convfmt = d->audio.convfmt;
		d->audio.convfmt.format = FFPCM_FLOAT;
		aa->state = 1;
		return FMED_RDATA;

	case 1:
		aa->state = 2;
		if (!(aa->orig_convfmt.format == FFPCM_FLOAT
			&& aa->orig_convfmt.ileaved)) {
			dbglog1(d->trk, "requesting audio conversion");
			d->audio.convfmt.format = FFPCM_FLOAT;
			d->audio.convfmt.ileaved = 1;
		}
		return FMED_RMORE;

	case 2:
		if (!(d->audio.convfmt.format == FFPCM_FLOAT
			&& d->audio.convfmt.ileaved)) {
			dbglog1(d->trk, "unsupported format");
			return FMED_RERR;
		}
		aa->state = 3;
	}

	float *f = (void*)d->data_in.ptr;
	ffsize nsamples = d->data_in.len / sizeof(float);

	if (nsamples == 0 && !(d->flags & FMED_FLAST))
		return FMED_RMORE;

	if (aa->user_gain_int != d->audio.gain) {
		aa->user_gain_int = d->audio.gain;
		aa->user_gain = ffpcm_db2gain((double)d->audio.gain / 100);
		dbglog1(d->trk, "ceiling:%.2f  aa-gain:%.2f  user-gain:%.2f  final-gain:%.2f"
			, aa->track_ceiling, aa->track_gain, aa->user_gain, aa->user_gain * aa->track_gain);
	}

	double gain = aa->user_gain * aa->track_gain;
	uint ich = 0;
	double sum = 0.0;

	for (ffsize i = 0;  i != nsamples;  i++) {

		sum += ffint_abs((double)f[i]);
		if (++ich == d->audio.convfmt.channels) {
			ich = 0;

			if (sum > aa->track_ceiling) {
				aa->track_ceiling = sum;

				sum /= d->audio.convfmt.channels;
				if (sum > 1.0)
					sum = 1.0;

				aa->track_gain = 1.0 - (sum - aa->ceiling);
				gain = aa->user_gain * aa->track_gain;
				dbglog1(d->trk, "ceiling:%.2f  aa-gain:%.2f  user-gain:%.2f  final-gain:%.2f"
					, aa->track_ceiling, aa->track_gain, aa->user_gain, gain);
			}

			sum = 0.0;
		}

		if (gain != 1.0)
			f[i] = f[i] * gain;
	}

	d->data_out = d->data_in;
	d->data_in.len = 0;
	return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RDATA;
}

const fmed_filter fmed_auto_attenuator = {
	aa_open, aa_process, aa_close
};
