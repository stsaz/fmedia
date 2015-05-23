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
	uint dsize;
	uint sample_size;
	uint byte_rate;
	uint64 dataoff;
	uint64 curpos;
	ffstr3 sample; //holds 1 incomplete sample
	ffbool parsed;
} fmed_wav;


//FMEDIA MODULE
static const fmed_filter* wav_iface(const char *name);
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

static int wav_parse(fmed_wav *w, fmed_filt *d);

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


static const fmed_filter* wav_iface(const char *name)
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
	return w;
}

static void wav_close(void *ctx)
{
	fmed_wav *w = ctx;
	ffarr_free(&w->sample);
	ffmem_free(w);
}

static int wav_parse(fmed_wav *w, fmed_filt *d)
{
	const ffwav_fmt *wf = NULL;
	const ffwav_data *wd;
	const char *data_first = d->data;
	uint64 total_size;
	uint br;
	const void *pchunk;

	for (;;) {
		size_t len = d->datalen;
		int r = ffwav_parse(d->data, &len);
		pchunk = d->data;
		d->data += len;
		d->datalen -= len;

		if (r & FFWAV_ERR) {
			errlog(core, d->trk, "wav", "invalid WAVE file", 0);
			goto done;
		}

		if (r & FFWAV_MORE) {
			errlog(core, d->trk, "wav", "unknown file format", 0);
			goto done;
		}

		switch (r) {
		case FFWAV_RFMT:
		case FFWAV_REXT:
			if (wf != NULL) {
				errlog(core, d->trk, "wav", "invalid WAVE file: duplicate format chunk", 0);
				goto done;
			}

			wf = pchunk;
			break;

		case FFWAV_RDATA:
			if (wf == NULL) {
				errlog(core, d->trk, "wav", "invalid WAVE file: no format chunk", 0);
				goto done;
			}

			wd = pchunk;
			goto data;
		}
	}

data:
	total_size = d->track->getval(d->trk, "total_size");
	if (total_size != FMED_NULL && wd->size != total_size - (d->data - data_first))
		errlog(core, d->trk, "wav", "size in header %u doesn't match file size %U"
			, wd->size, total_size);

	br = ffwav_bitrate(wf);
	d->track->setval(d->trk, "bitrate", br);

	w->sample_size = ffwav_samplesize(wf);
	if (NULL == ffarr_alloc(&w->sample, w->sample_size)) {
		errlog(core, d->trk, "wav", "%e", FFERR_BUFALOC);
		return -1;
	}

	d->track->setval(d->trk, "pcm_format", ffwav_pcmfmt(wf));
	d->track->setval(d->trk, "pcm_channels", wf->channels);
	d->track->setval(d->trk, "pcm_sample_rate", wf->sample_rate);

	w->byte_rate = ffwav_bitrate(wf) / 8;
	d->track->setval(d->trk, "total_samples", ffwav_samples(wf, wd->size));

	w->dataoff = d->data - data_first;
	w->parsed = 1;
	w->dsize = wd->size;
	return FMED_ROK;

done:
	return -1;
}

static int wav_process(void *ctx, fmed_filt *d)
{
	fmed_wav *w = ctx;
	uint n;
	uint64 seek_time, until_time;

	if (status == 1) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (!w->parsed)
		return wav_parse(w, d);

	if (w->dsize == 0) {
		errlog(core, d->trk, "wav", "reached the end of data chunk", 0);
		return -1;
	}

	//convert time position to file offset
	seek_time = d->track->popval(d->trk, "seek_time");
	if (seek_time != FMED_NULL) {
		uint64 seek_bytes = w->dataoff + w->byte_rate * (seek_time / 1000);
		dbglog(core, d->trk, "ogg", "seeking to %U sec (byte %U)", seek_time / 1000, seek_bytes);
		d->track->setval(d->trk, "input_seek", seek_bytes);
		w->curpos = ffpcm_samples(seek_time, d->track->getval(d->trk, "pcm_sample_rate"));
		return FMED_RMORE;
	}

	until_time = d->track->getval(d->trk, "until_time");
	if (until_time != FMED_NULL) {
		uint64 until_samples = ffpcm_samples(until_time, d->track->getval(d->trk, "pcm_sample_rate"));
		if (until_samples <= w->curpos) {
			dbglog(core, d->trk, "wav", "until_time is reached");
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
	}

	n = (uint)ffmin(w->dsize, d->datalen);

	if (w->sample.len != 0) {
		n = (uint)ffmin(n, ffarr_unused(&w->sample));
		ffmemcpy(ffarr_end(&w->sample), d->data, n);
		w->sample.len += n;

		d->data += n;
		d->datalen -= n;
		w->dsize -= n;
		if (!ffarr_isfull(&w->sample))
			return FMED_RMORE; //not even 1 complete PCM sample

		d->out = w->sample.ptr;
		d->outlen = w->sample.len;
		w->sample.len = 0;

	} else {

		size_t ntail = n % w->sample.cap;
		d->out = (void*)d->data;
		d->outlen = n - ntail;

		ffmemcpy(w->sample.ptr, d->data + n - ntail, ntail);
		w->sample.len = ntail;

		d->datalen -= n;
		w->dsize -= n;
		if (n == ntail)
			return FMED_RMORE; //not even 1 complete PCM sample
	}

	w->curpos += d->outlen / w->sample_size;
	d->track->setval(d->trk, "current_position", w->curpos);

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
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
		wav->wd.size += (uint)d->datalen;
		d->datalen = 0;
		return FMED_ROK;
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
		d->track->setval(d->trk, "total_size", fsz);
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
