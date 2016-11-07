/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/audio/soxr.h>
#include <FF/array.h>
#include <FF/crc.h>


static const fmed_core *core;

typedef struct sndmod_conv {
	uint state;
	uint out_samp_size;
	ffpcmex inpcm
		, outpcm;
	ffstr3 buf;
} sndmod_conv;

enum {
	CONV_OUTBUF_MSEC = 500,
};


//FMEDIA MODULE
static const void* sndmod_iface(const char *name);
static int sndmod_sig(uint signo);
static void sndmod_destroy(void);
static const fmed_mod fmed_sndmod_mod = {
	&sndmod_iface, &sndmod_sig, &sndmod_destroy
};

//CONVERTER
static void* sndmod_conv_open(fmed_filt *d);
static int sndmod_conv_process(void *ctx, fmed_filt *d);
static void sndmod_conv_close(void *ctx);
static const fmed_filter fmed_sndmod_conv = {
	&sndmod_conv_open, &sndmod_conv_process, &sndmod_conv_close
};

//CONVERTER-SOXR
static void* sndmod_soxr_open(fmed_filt *d);
static int sndmod_soxr_process(void *ctx, fmed_filt *d);
static void sndmod_soxr_close(void *ctx);
static const fmed_filter fmed_sndmod_soxr = {
	&sndmod_soxr_open, &sndmod_soxr_process, &sndmod_soxr_close
};

//GAIN
static void* sndmod_gain_open(fmed_filt *d);
static int sndmod_gain_process(void *ctx, fmed_filt *d);
static void sndmod_gain_close(void *ctx);
static const fmed_filter fmed_sndmod_gain = {
	&sndmod_gain_open, &sndmod_gain_process, &sndmod_gain_close
};

//UNTIL-TIME
static void* sndmod_untl_open(fmed_filt *d);
static int sndmod_untl_process(void *ctx, fmed_filt *d);
static void sndmod_untl_close(void *ctx);
static const fmed_filter fmed_sndmod_until = {
	&sndmod_untl_open, &sndmod_untl_process, &sndmod_untl_close
};

//PEAKS
static void* sndmod_peaks_open(fmed_filt *d);
static int sndmod_peaks_process(void *ctx, fmed_filt *d);
static void sndmod_peaks_close(void *ctx);
static const fmed_filter fmed_sndmod_peaks = {
	&sndmod_peaks_open, &sndmod_peaks_process, &sndmod_peaks_close
};

//RTPEAK
static void* sndmod_rtpeak_open(fmed_filt *d);
static int sndmod_rtpeak_process(void *ctx, fmed_filt *d);
static void sndmod_rtpeak_close(void *ctx);
static const fmed_filter fmed_sndmod_rtpeak = {
	&sndmod_rtpeak_open, &sndmod_rtpeak_process, &sndmod_rtpeak_close
};


const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_sndmod_mod;
}


static const void* sndmod_iface(const char *name)
{
	if (!ffsz_cmp(name, "conv"))
		return &fmed_sndmod_conv;
	else if (!ffsz_cmp(name, "conv-soxr"))
		return &fmed_sndmod_soxr;
	else if (!ffsz_cmp(name, "gain"))
		return &fmed_sndmod_gain;
	else if (!ffsz_cmp(name, "until"))
		return &fmed_sndmod_until;
	else if (!ffsz_cmp(name, "peaks"))
		return &fmed_sndmod_peaks;
	else if (!ffsz_cmp(name, "rtpeak"))
		return &fmed_sndmod_rtpeak;
	return NULL;
}

static int sndmod_sig(uint signo)
{
	return 0;
}

static void sndmod_destroy(void)
{
}


static void* sndmod_conv_open(fmed_filt *d)
{
	sndmod_conv *c = ffmem_tcalloc1(sndmod_conv);

	if (c == NULL)
		return NULL;
	return c;
}

static void sndmod_conv_close(void *ctx)
{
	sndmod_conv *c = ctx;
	ffarr_free(&c->buf);
	ffmem_free(c);
}

enum { CONV_CONF, CONV_CHK, CONV_DATA };

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
		, ffpcm_fmtstr(in->format), in->channels, in->sample_rate, (in->ileaved) ? "i" : "ni"
		, ffpcm_fmtstr(out->format), out->channels & FFPCM_CHMASK, out->sample_rate, (out->ileaved) ? "i" : "ni");
}

static int sndmod_conv_prepare(sndmod_conv *c, fmed_filt *d)
{
	size_t cap;

	c->outpcm.format = d->audio.convfmt.format;
	c->outpcm.ileaved = d->audio.convfmt.ileaved;
	if (d->audio.convfmt.channels != (c->outpcm.channels & FFPCM_CHMASK))
		c->outpcm.channels = d->audio.convfmt.channels;

	if (c->inpcm.format == c->outpcm.format
		&& c->inpcm.channels == c->outpcm.channels
		&& c->inpcm.ileaved == c->outpcm.ileaved) {
		d->out = d->data;
		d->outlen = d->datalen;
		return FMED_RDONE; //the second call of the module - no conversion is needed
	}

	if (c->inpcm.sample_rate != d->audio.convfmt.sample_rate) {
		if (c->inpcm.channels == c->outpcm.channels) {
			d->out = d->data;
			d->outlen = d->datalen;
			return FMED_RDONE;
		}

		c->outpcm.format = FFPCM_FLOAT;
		c->outpcm.sample_rate = c->inpcm.sample_rate;
		c->outpcm.ileaved = 0;
	}

	int r = ffpcm_convert(&c->outpcm, NULL, &c->inpcm, NULL, 0);
	if (r != 0 || (core->loglev == FMED_LOG_DEBUG)) {
		log_pcmconv("conv", r, &c->inpcm, &c->outpcm, d->trk);
		if (r != 0)
			return FMED_RERR;
	}

	uint out_ch = c->outpcm.channels & FFPCM_CHMASK;
	c->out_samp_size = ffpcm_size(c->outpcm.format, out_ch);
	cap = ffpcm_samples(CONV_OUTBUF_MSEC, c->outpcm.sample_rate) * c->out_samp_size;
	if (!c->outpcm.ileaved) {
		if (NULL == ffarr_alloc(&c->buf, sizeof(void*) * out_ch + cap)) {
			return FMED_RERR;
		}
		ffarrp_setbuf((void**)c->buf.ptr, out_ch, c->buf.ptr + sizeof(void*) * out_ch, cap / out_ch);

	} else {
		if (NULL == ffarr_alloc(&c->buf, cap))
			return FMED_RERR;
	}
	c->buf.len = cap / c->out_samp_size;

	c->state = CONV_DATA;
	return FMED_ROK;
}

/* PCM conversion filter is initialized in 2 steps:

1. The first time the converter is called, it just sets output audio format and returns with no data.
The conversion format may be already set by previous filters - the converter preserves those settings.
The next filters in chain may set the format they need, and then they ask for actual audio data.

2. The second time the converter is called, it checks whether conversion settings are supported and starts to convert audio data.
If input and output format settings are equal, the converter exits.
This filter can't convert sample rate - the next filter in chain must deal with it.
*/
static int sndmod_conv_process(void *ctx, fmed_filt *d)
{
	sndmod_conv *c = ctx;
	uint samples;
	int r;

	switch (c->state) {
	case CONV_CONF:
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
		c->state = CONV_CHK;
		return FMED_ROK;

	case CONV_CHK:
		r = sndmod_conv_prepare(c, d);
		if (c->state != CONV_DATA)
			return r;
		break;

	case CONV_DATA:
		break;
	}

	samples = (uint)ffmin(d->datalen / ffpcm_size1(&c->inpcm), c->buf.len);

	if (0 != ffpcm_convert(&c->outpcm, c->buf.ptr, &c->inpcm, d->data, samples)) {
		return FMED_RERR;
	}

	d->out = c->buf.ptr;
	d->outlen = samples * c->out_samp_size;
	d->datalen -= samples * ffpcm_size1(&c->inpcm);

	if (c->inpcm.ileaved)
		d->data += samples * ffpcm_size1(&c->inpcm);
	else if (samples != 0)
		ffarrp_shift((void**)d->datani, c->inpcm.channels, samples * ffpcm_size(c->inpcm.format, 1));

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
	return FMED_ROK;
}


typedef struct sndmod_soxr {
	uint state;
	ffsoxr soxr;
	ffpcm inpcm;
} sndmod_soxr;

static void* sndmod_soxr_open(fmed_filt *d)
{
	sndmod_soxr *c = ffmem_tcalloc1(sndmod_soxr);
	if (c == NULL)
		return NULL;
	ffsoxr_init(&c->soxr);
	return c;
}

static void sndmod_soxr_close(void *ctx)
{
	sndmod_soxr *c = ctx;
	ffsoxr_destroy(&c->soxr);
	ffmem_free(c);
}

/* How PCM sample rate conversion filter is initialized:
1. Return with no data so the next filters can set sample rate they need.

2. The second time the filter is called, check conversion sample rate.  If it equals to the input, then exit.
This filter converts both format and sample rate.
Previous filter must deal with channel conversion.  In this case the input format is float32/ni.
*/
static int sndmod_soxr_process(void *ctx, fmed_filt *d)
{
	sndmod_soxr *c = ctx;
	int val;
	ffpcmex inpcm, outpcm;

	switch (c->state) {
	case 0:
		d->outlen = 0;
		c->state = 1;
		return FMED_RDATA;

	case 1:
		if (d->audio.convfmt.sample_rate == d->audio.fmt.sample_rate)
			return FMED_RDONE_PREV;

		if (d->audio.convfmt.channels != d->audio.fmt.channels) {
			c->state = 2;
			return FMED_RMORE; // "conv" module will handle channel conversion
		}
		// break

	case 2:
		inpcm = d->audio.fmt;
		outpcm = d->audio.convfmt;
		if (c->state == 2) {
			inpcm.format = FFPCM_FLOAT;
			inpcm.channels = outpcm.channels;
			inpcm.ileaved = 0;
		}

		// c->soxr.dither = 1;
		if (0 != (val = ffsoxr_create(&c->soxr, &inpcm, &outpcm))
			|| (core->loglev == FMED_LOG_DEBUG)) {
			log_pcmconv("soxr", val, &inpcm, &outpcm, d->trk);
			if (val != 0)
				return FMED_RERR;
		}

		c->state = 3;
		break;

	case 3:
		break;
	}

	c->soxr.in_i = d->data;
	c->soxr.inlen = d->datalen;
	if (d->flags & FMED_FLAST)
		c->soxr.fin = 1;
	if (0 != ffsoxr_convert(&c->soxr)) {
		errlog(core, d->trk, "soxr", "ffsoxr_convert(): %s", ffsoxr_errstr(&c->soxr));
		return FMED_RERR;
	}

	d->out = c->soxr.out;
	d->outlen = c->soxr.outlen;

	if (c->soxr.outlen == 0) {
		if (d->flags & FMED_FLAST)
			return FMED_RDONE;
	}

	d->data = c->soxr.in_i;
	d->datalen = c->soxr.inlen;
	return FMED_ROK;
}


static void* sndmod_gain_open(fmed_filt *d)
{
	ffpcmex *pcm = ffmem_tcalloc1(ffpcmex);
	if (pcm == NULL)
		return NULL;
	*pcm = d->audio.fmt;
	return pcm;
}

static void sndmod_gain_close(void *ctx)
{
	ffpcmex *pcm = ctx;
	ffmem_free(pcm);
}

static int sndmod_gain_process(void *ctx, fmed_filt *d)
{
	ffpcmex *pcm = ctx;
	int db = d->audio.gain;
	if (db != FMED_NULL)
		ffpcm_gain(pcm, ffpcm_db2gain((double)db / 100), d->data, (void*)d->data, d->datalen / ffpcm_size1(pcm));

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}


typedef struct sndmod_untl {
	uint64 until;
	uint sampsize;
	uint asis :1; //data is passed as-is (i.e. encoded/compressed)
} sndmod_untl;

static void* sndmod_untl_open(fmed_filt *d)
{
	int64 val;
	sndmod_untl *u;

	if (FMED_NULL == (val = d->audio.until))
		return FMED_FILT_SKIP;

	if (NULL == (u = ffmem_tcalloc1(sndmod_untl)))
		return NULL;
	if (val > 0)
		u->until = ffpcm_samples(val, d->audio.fmt.sample_rate);
	else
		u->until = -val * d->audio.fmt.sample_rate / 75;

	u->sampsize = ffpcm_size(d->audio.fmt.format, d->audio.fmt.channels);

	if ((int64)d->audio.total != FMED_NULL)
		d->audio.total = u->until;

	if (FMED_PNULL != d->track->getvalstr(d->trk, "data_asis"))
		u->asis = 1;
	return u;
}

static void sndmod_untl_close(void *ctx)
{
	sndmod_untl *u = ctx;
	ffmem_free(u);
}

static int sndmod_untl_process(void *ctx, fmed_filt *d)
{
	sndmod_untl *u = ctx;
	uint samps;
	uint64 pos;

	d->out = d->data;
	d->outlen = d->datalen;

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;

	if (FMED_NULL == (int64)(pos = d->audio.pos))
		return FMED_RDONE;

	if (u->asis) {
		if (pos >= u->until) {
			dbglog(core, d->trk, "until", "reached sample #%U", u->until);
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
		d->datalen = 0;
		return FMED_ROK;
	}

	samps = d->datalen / u->sampsize;
	d->datalen = 0;
	if (pos + samps >= u->until) {
		dbglog(core, d->trk, "until", "reached sample #%U", u->until);
		d->outlen = (u->until > pos) ? (u->until - pos) * u->sampsize : 0;
		return FMED_RLASTOUT;
	}

	return FMED_ROK;
}


typedef struct sndmod_peaks {
	uint state;
	uint nch;
	uint64 total;

	struct {
		uint crc;
		uint high;
		uint64 sum;
		uint64 clipped;
	} ch[2];
	uint do_crc :1;
} sndmod_peaks;

static void* sndmod_peaks_open(fmed_filt *d)
{
	sndmod_peaks *p = ffmem_tcalloc1(sndmod_peaks);
	if (p == NULL)
		return NULL;

	p->nch = d->audio.convfmt.channels;
	if (p->nch > 2) {
		ffmem_free(p);
		return NULL;
	}

	p->do_crc = d->pcm_peaks_crc;
	return p;
}

static void sndmod_peaks_close(void *ctx)
{
	sndmod_peaks *p = ctx;
	ffmem_free(p);
}

static int sndmod_peaks_process(void *ctx, fmed_filt *d)
{
	sndmod_peaks *p = ctx;
	size_t i, ich, samples;

	switch (p->state) {
	case 0:
		d->audio.convfmt.ileaved = 0;
		d->audio.convfmt.format = FFPCM_16LE;
		p->state = 1;
		return FMED_RMORE;

	case 1:
		if (d->audio.convfmt.ileaved
			|| d->audio.convfmt.format != FFPCM_16LE) {
			errlog(core, d->trk, "peaks", "input must be non-interleaved 16LE PCM");
			return FMED_RERR;
		}
		p->state = 2;
		break;
	}

	samples = d->datalen / (sizeof(short) * p->nch);
	p->total += samples;

	for (ich = 0;  ich != p->nch;  ich++) {
		for (i = 0;  i != samples;  i++) {
			int sh = ((short**)d->datani)[ich][i];

			if (sh == 0x7fff || sh == -0x8000)
				p->ch[ich].clipped++;

			if (sh < 0)
				sh = -sh;

			if (p->ch[ich].high < (uint)sh)
				p->ch[ich].high = sh;

			p->ch[ich].sum += sh;
		}

		if (p->do_crc)
			p->ch[ich].crc = crc32(d->datani[ich], d->datalen / p->nch, p->ch[ich].crc);
	}

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST) {
		ffstr3 buf = {0};
		ffstr_catfmt(&buf, "\nPCM peaks (%,U total samples):\n"
			, p->total);

		if (p->total != 0) {
			for (ich = 0;  ich != p->nch;  ich++) {

				double hi = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].high));
				double avg = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].sum / p->total));
				ffstr_catfmt(&buf, "Channel #%L: highest peak:%.2FdB, avg peak:%.2FdB.  Clipped: %U (%.4F%%).  CRC:%08xu\n"
					, ich + 1, hi, avg
					, p->ch[ich].clipped, ((double)p->ch[ich].clipped * 100 / p->total)
					, p->ch[ich].crc);
			}
		}

		fffile_write(ffstdout, buf.ptr, buf.len);
		ffarr_free(&buf);
		return FMED_RDONE;
	}
	return FMED_ROK;
}


typedef struct sndmod_rtpeak {
	ffpcmex fmt;
} sndmod_rtpeak;

static void* sndmod_rtpeak_open(fmed_filt *d)
{
	sndmod_rtpeak *p = ffmem_tcalloc1(sndmod_rtpeak);
	if (p == NULL)
		return NULL;
	p->fmt = d->audio.fmt;
	float t;
	if (0 != ffpcm_peak(&p->fmt, NULL, 0, &t)) {
		errlog(core, d->trk, "rtpeak", "ffpcm_peak(): unsupported format");
		ffmem_free(p);
		return FMED_FILT_SKIP;
	}
	return p;
}

static void sndmod_rtpeak_close(void *ctx)
{
	sndmod_rtpeak *p = ctx;
	ffmem_free(p);
}

static int sndmod_rtpeak_process(void *ctx, fmed_filt *d)
{
	sndmod_rtpeak *p = ctx;

	float maxpeak;
	ffpcm_peak(&p->fmt, d->data, d->datalen / ffpcm_size1(&p->fmt), &maxpeak);
	double db = ffpcm_gain2db(maxpeak);
	d->audio.maxpeak = db;

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}
