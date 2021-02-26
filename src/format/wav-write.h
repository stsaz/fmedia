/** fmedia: .wav write
2015,2021, Simon Zolin */

#include <avpack/wav-write.h>

struct wavout {
	wavwrite wav;
	ffstr in;
	uint state;
};

void* wavout_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog1(d->trk, "unsupported input data format: %s", d->datatype);
		return NULL;
	}

	struct wavout *w = ffmem_new(struct wavout);
	return w;
}

void wavout_close(void *ctx)
{
	struct wavout *w = ctx;
	wavwrite_close(&w->wav);
	ffmem_free(w);
}

int wavout_process(void *ctx, fmed_filt *d)
{
	struct wavout *w = ctx;
	int r;

	switch (w->state) {
	case 0:
		w->state = 1;
		if (!d->audio.convfmt.ileaved) {
			d->audio.convfmt.ileaved = 1;
			return FMED_RMORE;
		}
		// fallthrough

	case 1: {
		struct wav_info info = {};
		info.format = d->audio.convfmt.format;
		if (d->audio.convfmt.format == FFPCM_FLOAT)
			info.format = WAV_FLOAT;
		info.sample_rate = d->audio.convfmt.sample_rate;
		info.channels = d->audio.convfmt.channels;
		if ((int64)d->audio.total != FMED_NULL)
			info.total_samples = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);
		wavwrite_create(&w->wav, &info);
		w->state = 2;
	}
		// fallthrough

	case 2:
		break;
	}

	if (d->flags & FMED_FFWD) {
		w->in = d->data_in;
		d->data_in.len = 0;
	}
	if (d->flags & FMED_FLAST)
		wavwrite_finish(&w->wav);

	for (;;) {
		r = wavwrite_process(&w->wav, &w->in, &d->data_out);
		switch (r) {
		case WAVWRITE_MORE:
			return FMED_RMORE;

		case WAVWRITE_DONE:
			d->data_out.len = 0;
			return FMED_RDONE;

		case WAVWRITE_HEADER:
			d->output.size = wavwrite_size(&w->wav);
			// fallthrough

		case WAVWRITE_DATA:
			goto data;

		case WAVWRITE_SEEK:
			if (!d->out_seekable) {
				warnlog1(d->trk, "can't seek to finalize WAV header");
				d->data_out.len = 0;
				return FMED_RDONE;
			}
			d->output.seek = wavwrite_offset(&w->wav);
			continue;

		case WAVWRITE_ERROR:
			errlog1(d->trk, "wavwrite_process(): %s", wavwrite_error(&w->wav));
			return FMED_RERR;
		}
	}

data:
	dbglog1(d->trk, "output: %L bytes", d->data_out.len);
	return FMED_RDATA;
}

const fmed_filter wav_output = {
	wavout_open, wavout_process, wavout_close
};
