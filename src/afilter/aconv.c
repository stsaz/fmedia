/** Audio converter.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <afilter/pcm.h>

extern const fmed_core *core;

enum {
	CONV_OUTBUF_MSEC = 500,
};

typedef struct aconv {
	uint state;
	uint out_samp_size;
	ffpcmex inpcm
		, outpcm;
	ffvec buf;
	uint off;
} aconv;

static void* aconv_open(fmed_filt *d)
{
	aconv *c = ffmem_new(aconv);
	return c;
}

static void aconv_close(void *ctx)
{
	aconv *c = ctx;
	ffmem_alignfree(c->buf.ptr);
	ffmem_free(c);
}

static void log_pcmconv(const char *module, int r, const ffpcmex *in, const ffpcmex *out, void *trk)
{
	int f = FMED_LOG_DEBUG;
	const char *unsupp = "";
	if (r != 0) {
		f = FMED_LOG_ERR;
		unsupp = "unsupported ";
	}
	core->log(f, trk, module, "%sPCM conversion: %s/%u/%u/%s -> %s/%u/%u/%s"
		, unsupp
		, ffpcm_fmtstr(in->format), in->sample_rate, in->channels, (in->ileaved) ? "i" : "ni"
		, ffpcm_fmtstr(out->format), out->sample_rate, (out->channels & FFPCM_CHMASK), (out->ileaved) ? "i" : "ni");
}

static int aconv_prepare(aconv *c, fmed_filt *d)
{
	c->inpcm = d->aconv.in;
	c->outpcm = d->aconv.out;

	size_t cap;
	const ffpcmex *in = &c->inpcm;
	const ffpcmex *out = &c->outpcm;

	if (in->sample_rate != out->sample_rate) {

		const struct fmed_filter2 *soxr = core->getmod("soxr.conv");
		void *f = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "soxr.conv");
		if (f == NULL)
			return FMED_RERR;
		void *fi = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_INSTANCE, f);
		if (fi == NULL)
			return FMED_RERR;
		struct fmed_aconv conf = {};

		if (in->channels == out->channels) {
			// The next filter will convert format and sample rate:
			// soxr
			conf.in = *in;
			conf.out = *out;
			d->out = d->data;
			d->outlen = d->datalen;
			soxr->cmd(fi, 0, &conf);
			return FMED_RDONE;
		}

		// This filter will convert channels, the next filter will convert format and sample rate:
		// conv -> soxr
		conf.out = c->outpcm;

		c->outpcm.format = FFPCM_FLOAT;
		c->outpcm.sample_rate = in->sample_rate;
		c->outpcm.ileaved = 0;

		conf.in = c->outpcm;
		conf.in.channels = (c->outpcm.channels & FFPCM_CHMASK);
		soxr->cmd(fi, 0, &conf);
	}

	if (c->inpcm.channels > 8)
		return FMED_RERR;

	int r = ffpcm_convert(&c->outpcm, NULL, &c->inpcm, NULL, 0);
	if (r != 0 || (core->loglev == FMED_LOG_DEBUG)) {
		log_pcmconv("conv", r, &c->inpcm, &c->outpcm, d->trk);
		if (r != 0)
			return FMED_RERR;
	}

	uint out_ch = c->outpcm.channels & FFPCM_CHMASK;
	c->out_samp_size = ffpcm_size(c->outpcm.format, out_ch);
	cap = ffpcm_samples(CONV_OUTBUF_MSEC, c->outpcm.sample_rate) * c->out_samp_size;
	uint n = cap;
	if (!c->outpcm.ileaved)
		n = sizeof(void*) * out_ch + cap;
	if (NULL == (c->buf.ptr = ffmem_align(n, 16)))
		return FMED_RERR;
	c->buf.cap = n;
	if (!c->outpcm.ileaved) {
		ffarrp_setbuf((void**)c->buf.ptr, out_ch, c->buf.ptr + sizeof(void*) * out_ch, cap / out_ch);
	}
	c->buf.len = cap / c->out_samp_size;

	return FMED_ROK;
}

static int aconv_process(void *ctx, fmed_filt *d)
{
	aconv *c = ctx;
	uint samples;
	int r;

	switch (c->state) {
	case 0:
		r = aconv_prepare(c, d);
		if (r != FMED_ROK)
			return r;
		c->state = 2;
		break;

	case 2:
		break;
	}

	if (d->flags & FMED_FFWD)
		c->off = 0;

	samples = (uint)ffmin(d->datalen / ffpcm_size1(&c->inpcm), c->buf.len);
	if (samples == 0) {
		if (d->flags & FMED_FLAST)
			return FMED_RDONE;
		return FMED_RMORE;
	}

	void *in[8];
	const void *data;
	if (!c->inpcm.ileaved) {
		for (uint i = 0;  i != c->inpcm.channels;  i++) {
			in[i] = (char*)d->datani[i] + c->off;
		}
		data = in;
	} else {
		data = (char*)d->data + c->off * c->inpcm.channels;
	}

	if (0 != ffpcm_convert(&c->outpcm, c->buf.ptr, &c->inpcm, data, samples)) {
		return FMED_RERR;
	}

	d->out = c->buf.ptr;
	d->outlen = samples * c->out_samp_size;
	d->datalen -= samples * ffpcm_size1(&c->inpcm);
	c->off += samples * ffpcm_size(c->inpcm.format, 1);
	return FMED_RDATA;
}

const fmed_filter fmed_sndmod_conv = {
	aconv_open, aconv_process, aconv_close
};


/* Audio converter that is automatically added into chain when track is created.
The filter is initialized in 2 steps:

1. The first time the converter is called, it just sets output audio format and returns with no data.
The conversion format may be already set by previous filters - the converter preserves those settings.
The next filters in chain may set the format they need, and then they ask for actual audio data.

2. The second time the converter is called, it initializes afilter.conv filter if needed, and then deletes itself from chain.
*/

struct autoconv {
	uint state;
	ffpcmex inpcm, outpcm;
};

static void* autoconv_open(fmed_filt *d)
{
	if (d->stream_copy) {
		if (ffsz_eq(d->datatype, "pcm")) {
			errlog(core, d->trk, "afilter.autoconv", "decoder doesn't support --stream-copy", 0);
			return NULL;
		}
		d->audio.convfmt = d->audio.fmt;
		return FMED_FILT_SKIP;
	}

	struct autoconv *c = ffmem_new(struct autoconv);
	return c;
}

static void autoconv_close(void *ctx)
{
	ffmem_free(ctx);
}

static int autoconv_process(void *ctx, fmed_filt *d)
{
	struct autoconv *c = ctx;
	switch (c->state) {
	case 0:
		c->inpcm = d->audio.fmt;
		c->outpcm = d->audio.convfmt;
		if (d->audio.convfmt.format == 0)
			c->outpcm.format = d->audio.fmt.format;
		if (d->audio.convfmt.channels == 0)
			c->outpcm.channels = d->audio.fmt.channels;
		if (d->audio.convfmt.sample_rate == 0)
			c->outpcm.sample_rate = d->audio.fmt.sample_rate;
		c->outpcm.ileaved = d->audio.fmt.ileaved;
		d->audio.convfmt = c->outpcm;
		d->audio.convfmt.channels = (c->outpcm.channels & FFPCM_CHMASK);
		d->outlen = 0;
		c->state = 1;
		return FMED_RDATA;
	case 1:
		break;
	}

	const ffpcmex *in = &d->audio.fmt;
	const ffpcmex *out = &d->audio.convfmt;
	if ((in->format != c->outpcm.format && c->outpcm.format != out->format)
		|| (in->channels != c->outpcm.channels && (c->outpcm.channels & FFPCM_CHMASK) != out->channels)
		|| (in->sample_rate != c->outpcm.sample_rate && c->outpcm.sample_rate != out->sample_rate))
		dbglog(core, d->trk, NULL, "conversion format was overwritten by output filters: %s/%u/%u"
			, ffpcm_fmtstr(out->format), out->channels, out->sample_rate);

	if (in->format == out->format
		&& in->channels == out->channels
		&& in->sample_rate == out->sample_rate
		&& in->ileaved == out->ileaved) {
		d->out = d->data,  d->outlen = d->datalen;
		return FMED_RDONE; //no conversion is needed
	}

	void *f = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "afilter.conv");
	if (f == NULL)
		return FMED_RERR;

	d->aconv.in = *in;
	d->aconv.out = *out;

	if ((c->outpcm.channels & FFPCM_CHMASK) == d->aconv.out.channels
		&& (c->outpcm.channels & ~FFPCM_CHMASK) != 0)
		d->aconv.out.channels = c->outpcm.channels;

	d->out = d->data,  d->outlen = d->datalen;
	return FMED_RDONE;
}

const fmed_filter fmed_sndmod_autoconv = { autoconv_open, autoconv_process, autoconv_close };
