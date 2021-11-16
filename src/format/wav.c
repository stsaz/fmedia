/** WAVE input/output, RAW input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <avpack/wav-read.h>
#include <FF/audio/pcm.h>
#include <format/mmtag.h>


extern const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <format/wav-write.h>

typedef struct fmed_wav {
	wavread wav;
	void *trk;
	ffstr in;
	uint srate;
	uint state;
} fmed_wav;

void wav_log(void *udata, ffstr msg)
{
	fmed_wav *w = udata;
	dbglog1(w->trk, "%S", &msg);
}

static void* wav_open(fmed_filt *d)
{
	fmed_wav *w = ffmem_tcalloc1(fmed_wav);
	if (w == NULL) {
		errlog1(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}
	w->trk = d->trk;
	wavread_open(&w->wav);
	w->wav.log = wav_log;
	w->wav.udata = w;
	d->duration_accurate = 1;
	return w;
}

static void wav_close(void *ctx)
{
	fmed_wav *w = ctx;
	wavread_close(&w->wav);
	ffmem_free(w);
}

static void wav_meta(fmed_wav *w, fmed_filt *d)
{
	ffstr name, val;
	int tag = wavread_tag(&w->wav, &val);
	if (tag == 0)
		return;
	ffstr_setz(&name, ffmmtag_str[tag]);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static int wav_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_wav *w = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		w->in = d->data_in;
		d->data_in.len = 0;
	}

again:
	switch (w->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			wavread_seek(&w->wav, ffpcm_samples(d->audio.seek, w->srate));
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	if (d->flags & FMED_FLAST)
		w->wav.fin = 1;

	for (;;) {
		r = wavread_process(&w->wav, &w->in, &d->data_out);
		switch (r) {
		case WAVREAD_MORE:
			if (d->flags & FMED_FLAST) {
				if (!w->wav.inf_data)
					errlog1(d->trk, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case WAVREAD_DONE:
			d->outlen = 0;
			return FMED_RDONE;

		case WAVREAD_DATA:
			goto data;

		case WAVREAD_HEADER: {
			const struct wav_info *ai = wavread_info(&w->wav);
			d->audio.decoder = "WAVE";
			w->srate = ai->sample_rate;
			d->audio.fmt.format = ai->format;
			if (ai->format == WAV_FLOAT)
				d->audio.fmt.format = FFPCM_FLOAT;
			d->audio.fmt.channels = ai->channels;
			d->audio.fmt.sample_rate = ai->sample_rate;
			d->audio.fmt.ileaved = 1;
			d->audio.total = ai->total_samples;
			d->audio.bitrate = ai->bitrate;
			d->datatype = "pcm";
			w->state = I_DATA;
			goto again;
		}

		case WAVREAD_TAG:
			wav_meta(w, d);
			break;

		case WAVREAD_SEEK:
			d->input.seek = wavread_offset(&w->wav);
			return FMED_RMORE;

		case WAVREAD_ERROR:
		default:
			errlog1(d->trk, "wavread_decode(): %s", wavread_error(&w->wav));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = wavread_cursample(&w->wav);
	return FMED_RDATA;
}

const fmed_filter wav_input = {
	wav_open, wav_process, wav_close
};


typedef struct raw {
	uint64 curpos;
} raw;

static void* raw_open(fmed_filt *d)
{
	raw *r = ffmem_tcalloc1(raw);
	if (r == NULL) {
		errlog1(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}

	if ((int64)d->input.size != FMED_NULL)
		d->audio.total = d->input.size / ffpcm_size(FFPCM_16LE, 2);

	d->audio.bitrate = 44100 * ffpcm_size(FFPCM_16LE, 2) * 8;
	d->audio.fmt.format = FFPCM_16LE;
	d->audio.fmt.channels = 2;
	d->audio.fmt.sample_rate = 44100;
	d->audio.fmt.ileaved = 1;
	return r;
}

static void raw_close(void *ctx)
{
	raw *r = ctx;
	ffmem_free(r);
}

static int raw_read(void *ctx, fmed_filt *d)
{
	raw *r = ctx;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	r->curpos += d->outlen / ffpcm_size(FFPCM_16LE, 2);
	d->audio.pos = r->curpos;

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
	return FMED_ROK;
}

const fmed_filter raw_input = {
	raw_open, raw_read, raw_close
};
