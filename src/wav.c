/** WAVE input/output, RAW input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wav.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FFOS/error.h>


static const fmed_core *core;
static uint status;

typedef struct fmed_wav {
	ffwav wav;
	uint state;
} fmed_wav;


//FMEDIA MODULE
static const void* wav_iface(const char *name);
static int wav_sig(uint signo);
static void wav_destroy(void);
static const fmed_mod fmed_wav_mod = {
	&wav_iface, &wav_sig, &wav_destroy
};

//INPUT
static void* wav_open(fmed_filt *d);
static void wav_close(void *ctx);
static int wav_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_wav_input = {
	&wav_open, &wav_process, &wav_close
};

//OUTPUT
static void* wavout_open(fmed_filt *d);
static void wavout_close(void *ctx);
static int wavout_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_wav_output = {
	&wavout_open, &wavout_process, &wavout_close
};


typedef struct raw {
	uint64 curpos;
} raw;

//INPUT
static void* raw_open(fmed_filt *d);
static int raw_read(void *ctx, fmed_filt *d);
static void raw_close(void *ctx);
static const fmed_filter fmed_raw_input = {
	&raw_open, &raw_read, &raw_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_wav_mod;
}


static const void* wav_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_wav_input;
	else if (!ffsz_cmp(name, "out"))
		return &fmed_wav_output;
	else if (!ffsz_cmp(name, "rawin"))
		return &fmed_raw_input;
	return NULL;
}

static int wav_sig(uint signo)
{
	switch (signo) {
	case FMED_STOP:
		status = 1;
		break;
	}
	return 0;
}

static void wav_destroy(void)
{
}


static void* wav_open(fmed_filt *d)
{
	fmed_wav *w = ffmem_tcalloc1(fmed_wav);
	if (w == NULL) {
		errlog(core, d->trk, "wav", "%e", FFERR_BUFALOC);
		return NULL;
	}
	ffwav_init(&w->wav);
	return w;
}

static void wav_close(void *ctx)
{
	fmed_wav *w = ctx;
	ffwav_close(&w->wav);
	ffmem_free(w);
}

static int wav_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_wav *w = ctx;
	int r;
	int64 seek_time, until_time;

	if (status == 1) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	w->wav.data = d->data;
	w->wav.datalen = d->datalen;

again:
	switch (w->state) {
	case I_HDR:
		break;

	case I_DATA:
		if (FMED_NULL != (seek_time = fmed_popval("seek_time")))
			ffwav_seek(&w->wav, ffpcm_samples(seek_time, ffwav_rate(&w->wav)));
		break;
	}

	for (;;) {
		r = ffwav_decode(&w->wav);
		switch (r) {
		case FFWAV_RMORE:
			if (d->flags & FMED_FLAST) {
				errlog(core, d->trk, "wav", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFWAV_RDONE:
			if (!(d->flags & FMED_FLAST))
				errlog(core, d->trk, "wav", "skipping some data at the end of file");
			d->outlen = 0;
			return FMED_RDONE;

		case FFWAV_RDATA:
			goto data;

		case FFWAV_RHDR:
			fmed_setval("pcm_format", w->wav.fmt.format);
			fmed_setval("pcm_channels", w->wav.fmt.channels);
			fmed_setval("pcm_sample_rate", w->wav.fmt.sample_rate);
			fmed_setval("total_samples", w->wav.total_samples);
			fmed_setval("bitrate", w->wav.bitrate);
			w->state = I_DATA;
			goto again;

		case FFWAV_RSEEK:
			fmed_setval("input_seek", ffwav_seekoff(&w->wav));
			return FMED_RMORE;

		case FFWAV_RERR:
		default:
			errlog(core, d->trk, "wav", "ffwav_decode(): %s", ffwav_errstr(&w->wav));
			return FMED_RERR;
		}
	}

data:
	if (FMED_NULL != (until_time = d->track->getval(d->trk, "until_time"))) {
		uint64 until_samples = ffpcm_samples(until_time, ffwav_rate(&w->wav));
		if (until_samples <= ffwav_cursample(&w->wav)) {
			dbglog(core, d->trk, "wav", "until_time is reached");
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
	}

	fmed_setval("current_position", ffwav_cursample(&w->wav));
	d->data = w->wav.data;
	d->datalen = w->wav.datalen;
	d->out = w->wav.pcm;
	d->outlen = w->wav.pcmlen;
	return FMED_ROK;
}


static void* wavout_open(fmed_filt *d)
{
	ffwavpcmhdr *wav = ffmem_tcalloc1(ffwavpcmhdr);
	return wav;
}

static void wavout_close(void *ctx)
{
	ffwavpcmhdr *wav = ctx;
	ffmem_free(wav);
}

static int wavout_process(void *ctx, fmed_filt *d)
{
	ffwavpcmhdr *wav = ctx;
	uint64 total_dur;
	int il;

	if (wav->wf.format != 0) {

		if (wav->wd.size + d->datalen > (uint)-1) {
			errlog(core, d->trk, "wav", "max file size is 4gb", 0);
			return -1;
		}

		if ((d->flags & FMED_FLAST) && d->datalen == 0) {
			wav->wr.size = wav->wd.size + sizeof(ffwavpcmhdr) - 8;
			d->out = (void*)wav;
			d->outlen = sizeof(ffwav_pcmhdr);
			d->track->setval(d->trk, "output_seek", 0);
			return FMED_RDONE;
		}

		d->out = d->data;
		d->outlen = d->datalen;
		dbglog(core, d->trk, "wav", "passed %L samples", d->datalen / wav->wf.block_align);
		wav->wd.size += (uint)d->datalen;
		d->datalen = 0;
		return FMED_ROK;
	}

	il = (int)fmed_getval("pcm_ileaved");
	if (il != 1) {
		fmed_setval("conv_pcm_ileaved", 1);
		return FMED_RMORE;
	}

	*wav = ffwav_pcmhdr;
	ffwav_pcmfmtset(&wav->wf, (int)d->track->getval(d->trk, "pcm_format"));
	wav->wf.sample_rate = (int)d->track->getval(d->trk, "pcm_sample_rate");
	wav->wf.channels = (int)d->track->getval(d->trk, "pcm_channels");
	ffwav_setbr(&wav->wf);

	if (wav->wf.format == 0 || wav->wf.sample_rate == 0 || wav->wf.channels == 0) {
		errlog(core, d->trk, "wav", "invalid PCM format");
		return FMED_RERR;
	}

	d->out = (void*)wav;
	d->outlen = sizeof(ffwav_pcmhdr);

	total_dur = d->track->getval(d->trk, "total_samples");
	if (total_dur != FMED_NULL) {
		uint64 fsz = sizeof(ffwavpcmhdr) + total_dur * ffwav_samplesize(&wav->wf);
		d->track->setval(d->trk, "output_size", fsz);
	}

	return FMED_ROK;
}


static void* raw_open(fmed_filt *d)
{
	uint64 total_size;
	raw *r = ffmem_tcalloc1(raw);
	if (r == NULL) {
		errlog(core, d->trk, "raw", "%e", FFERR_BUFALOC);
		return NULL;
	}

	total_size = d->track->getval(d->trk, "total_size");
	if (total_size != FMED_NULL)
		d->track->setval(d->trk, "total_samples", total_size / ffpcm_size(FFPCM_16LE, 2));

	d->track->setval(d->trk, "bitrate", 44100 * ffpcm_size(FFPCM_16LE, 2) * 8);
	d->track->setval(d->trk, "pcm_format", FFPCM_16LE);
	d->track->setval(d->trk, "pcm_channels", 2);
	d->track->setval(d->trk, "pcm_sample_rate", 44100);
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

	if (status == 1) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	r->curpos += d->outlen / ffpcm_size(FFPCM_16LE, 2);
	d->track->setval(d->trk, "current_position", r->curpos);

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
	return FMED_ROK;
}
