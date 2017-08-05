/** WAVE input/output, RAW input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wav.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>
#include <FF/array.h>
#include <FFOS/error.h>


static const fmed_core *core;
static const fmed_queue *qu;

typedef struct fmed_wav {
	ffwav wav;
	uint state;
} fmed_wav;


//FMEDIA MODULE
static const void* wav_iface(const char *name);
static int wav_sig(uint signo);
static void wav_destroy(void);
static const fmed_mod fmed_wav_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
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
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
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
		errlog(core, d->trk, "wav", "%s", ffmem_alloc_S);
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

static void wav_meta(fmed_wav *w, fmed_filt *d)
{
	ffstr name, val;
	if (w->wav.tag == -1)
		return;
	ffstr_setz(&name, ffmmtag_str[w->wav.tag]);
	ffstr_set2(&val, &w->wav.tagval);
	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
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

	w->wav.data = d->data;
	w->wav.datalen = d->datalen;

again:
	switch (w->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			ffwav_seek(&w->wav, ffpcm_samples(d->audio.seek, ffwav_rate(&w->wav)));
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	if (d->flags & FMED_FLAST)
		w->wav.fin = 1;

	for (;;) {
		r = ffwav_decode(&w->wav);
		switch (r) {
		case FFWAV_RMORE:
			if (d->flags & FMED_FLAST) {
				if (!w->wav.inf_data)
					errlog(core, d->trk, "wav", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFWAV_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFWAV_RDATA:
			goto data;

		case FFWAV_RHDR:
			d->audio.decoder = "WAVE";
			ffpcm_fmtcopy(&d->audio.fmt, &w->wav.fmt);
			d->audio.fmt.ileaved = 1;
			d->audio.total = w->wav.total_samples;
			d->audio.bitrate = w->wav.bitrate;
			d->datatype = "pcm";
			w->state = I_DATA;
			goto again;

		case FFWAV_RTAG:
			wav_meta(w, d);
			break;

		case FFWAV_RSEEK:
			d->input.seek = ffwav_seekoff(&w->wav);
			return FMED_RMORE;

		case FFWAV_RWARN:
			warnlog(core, d->trk, "wav", "ffwav_decode(): %s", ffwav_errstr(&w->wav));
			break;

		case FFWAV_RERR:
		default:
			errlog(core, d->trk, "wav", "ffwav_decode(): %s", ffwav_errstr(&w->wav));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = ffwav_cursample(&w->wav);
	d->data = w->wav.data;
	d->datalen = w->wav.datalen;
	d->out = w->wav.pcm;
	d->outlen = w->wav.pcmlen;
	return FMED_ROK;
}


typedef struct wavout {
	uint state;
	ffwav_cook wav;
} wavout;

static void* wavout_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
		return NULL;
	}

	wavout *wav = ffmem_tcalloc1(wavout);
	return wav;
}

static void wavout_close(void *ctx)
{
	wavout *wav = ctx;
	ffmem_free(wav);
}

static int wavout_process(void *ctx, fmed_filt *d)
{
	wavout *w = ctx;
	uint64 total_dur = 0;
	int r;

	switch (w->state) {
	case 0:
		d->audio.convfmt.ileaved = 1;
		w->state = 1;
		return FMED_RMORE;

	case 1: {
		ffpcm fmt;
		ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
		if ((int64)d->audio.total != FMED_NULL)
			total_dur = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);
		ffwav_create(&w->wav, &fmt, total_dur);
		if (!d->out_seekable)
			w->wav.seekable = 0;

		w->state = 2;
		// break
	}

	case 2:
		break;
	}

	w->wav.pcm = d->data,  w->wav.pcmlen = d->datalen;
	if (d->flags & FMED_FLAST)
		w->wav.fin = 1;

	for (;;) {
	r = ffwav_write(&w->wav);
	switch (r) {
	case FFWAV_RMORE:
		return FMED_RMORE;

	case FFWAV_RDONE:
		d->outlen = 0;
		return FMED_RDONE;

	case FFWAV_RHDR:
		d->output.size = ffwav_wsize(&w->wav);
		// break

	case FFWAV_RDATA:
		goto data;

	case FFWAV_RSEEK:
		d->output.seek = ffwav_wseekoff(&w->wav);
		continue;

	case FFWAV_RERR:
		errlog(core, d->trk, "wav", "ffwav_write(): %s", ffwav_errstr(&w->wav));
		return FMED_RERR;
	}
	}

data:
	d->data = w->wav.pcm,  d->datalen = w->wav.pcmlen;
	dbglog(core, d->trk, "wav", "output: %L bytes", w->wav.datalen);
	d->out = w->wav.data,  d->outlen = w->wav.datalen;
	return FMED_RDATA;
}


static void* raw_open(fmed_filt *d)
{
	raw *r = ffmem_tcalloc1(raw);
	if (r == NULL) {
		errlog(core, d->trk, "raw", "%s", ffmem_alloc_S);
		return NULL;
	}

	if ((int64)d->input.size != FMED_NULL)
		d->audio.total = d->input.size / ffpcm_size(FFPCM_16LE, 2);

	d->audio.bitrate = 44100 * ffpcm_size(FFPCM_16LE, 2) * 8;
	d->audio.fmt.format = FFPCM_16LE;
	d->audio.fmt.channels = 2;
	d->audio.fmt.sample_rate = 44100;
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
