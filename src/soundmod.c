/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/audio/soxr.h>
#include <FF/array.h>
#include <FF/crc.h>

/*
PCM conversion preparation:
 . INPUT -> conv -> conv-soxr -> OUTPUT

                                newfmt+rate
 . INPUT -- [conv] -- conv-soxr      <-     OUTPUT

                                newfmt
 . INPUT -- conv <- [conv-soxr]   <-   OUTPUT
*/

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
	int il, fmt, val;
	size_t cap;

	fmt = (int)fmed_popval("conv_pcm_format");
	il = (int)fmed_popval("conv_pcm_ileaved");

	if (fmt != FMED_NULL) {
		c->outpcm.format = fmt;
		fmed_setval("pcm_format", c->outpcm.format);
	}

	if (il != FMED_NULL) {
		c->outpcm.ileaved = il;
		fmed_setval("pcm_ileaved", c->outpcm.ileaved);
	}

	if (FMED_NULL != (val = fmed_popval("conv_channels"))) {
		c->outpcm.channels = val;
		fmed_setval("pcm_channels", val & FFPCM_CHMASK);
	}

	if (!ffmemcmp(&c->outpcm, &c->inpcm, sizeof(ffpcmex))) {
		d->out = d->data;
		d->outlen = d->datalen;
		return FMED_RDONE; //the second call of the module - no conversion is needed
	}

	int r = ffpcm_convert(&c->outpcm, NULL, &c->inpcm, NULL, 0);
	if (r != 0 || (core->loglev & FMED_LOG_DEBUG)) {
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

static int sndmod_conv_process(void *ctx, fmed_filt *d)
{
	sndmod_conv *c = ctx;
	uint samples;
	int r;

	switch (c->state) {
	case CONV_CONF:
		c->inpcm.format = (int)fmed_getval("pcm_format");
		c->inpcm.sample_rate = (int)fmed_getval("pcm_sample_rate");
		c->inpcm.channels = (int)fmed_getval("pcm_channels");
		if (1 == fmed_getval("pcm_ileaved"))
			c->inpcm.ileaved = 1;
		c->outpcm = c->inpcm;
		if (FMED_NULL != (r = fmed_popval("conv_channels"))) {
			c->outpcm.channels = r;
			fmed_setval("pcm_channels", r & FFPCM_CHMASK);
		}

		d->outlen = 0;
		c->state = CONV_CHK;
		return FMED_ROK;

	case CONV_CHK:
		r = sndmod_conv_prepare(c, d);
		if (c->state != 2)
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

static int sndmod_soxr_process(void *ctx, fmed_filt *d)
{
	sndmod_soxr *c = ctx;
	int val;
	ffpcmex inpcm, outpcm;

	switch (c->state) {
	case 0:
		c->inpcm.sample_rate = (int)fmed_getval("pcm_sample_rate");
		if (FMED_NULL != (int)(outpcm.sample_rate = (int)fmed_getval("conv_pcm_rate")))
			fmed_setval("pcm_sample_rate", outpcm.sample_rate);
		d->outlen = 0;
		c->state = 1;
		return FMED_RDATA;

	case 1:
		if (FMED_NULL == fmed_getval("conv_pcm_rate"))
			return FMED_RDONE_PREV;

		if (FMED_NULL != fmed_getval("conv_channels"))
			return FMED_RMORE; // "conv" module will handle channel conversion

		inpcm.format = (int)fmed_getval("pcm_format");
		inpcm.channels = (int)fmed_getval("pcm_channels");
		inpcm.sample_rate = c->inpcm.sample_rate;
		if (1 == fmed_getval("pcm_ileaved"))
			inpcm.ileaved = 1;
		outpcm = inpcm;

		outpcm.sample_rate = (int)fmed_popval("conv_pcm_rate");
		fmed_setval("pcm_sample_rate", outpcm.sample_rate);

		if (FMED_NULL != (val = (int)fmed_popval("conv_pcm_format"))) {
			outpcm.format = val;
			fmed_setval("pcm_format", outpcm.format);
		}

		if (FMED_NULL != (val = (int)fmed_popval("conv_pcm_ileaved"))) {
			outpcm.ileaved = val;
			fmed_setval("pcm_ileaved", outpcm.ileaved);
		}

		if (!ffmemcmp(&outpcm, &inpcm, sizeof(ffpcmex))) {
			d->out = d->data;
			d->outlen = d->datalen;
			return FMED_RDONE;
		}

		// c->soxr.dither = 1;
		if (0 != (val = ffsoxr_create(&c->soxr, &inpcm, &outpcm))
			|| (core->loglev & FMED_LOG_DEBUG)) {
			log_pcmconv("soxr", val, &inpcm, &outpcm, d->trk);
			if (val != 0)
				return FMED_RERR;
		}

		c->state = 2;
		break;

	case 2:
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
	pcm->format = (int)fmed_getval("pcm_format");
	pcm->channels = (int)fmed_getval("pcm_channels");
	pcm->ileaved = (int)fmed_getval("pcm_ileaved");
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
	int db = (int)fmed_getval("gain");
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
	int64 val, rate;
	sndmod_untl *u;

	if (FMED_NULL == (val = fmed_getval("until_time")))
		return FMED_FILT_SKIP;

	rate = fmed_getval("pcm_sample_rate");

	if (NULL == (u = ffmem_tcalloc1(sndmod_untl)))
		return NULL;
	if (val > 0)
		u->until = ffpcm_samples(val, rate);
	else
		u->until = -val * rate / 75;

	val = fmed_getval("pcm_format");
	u->sampsize = ffpcm_size(val, fmed_getval("pcm_channels"));

	if (FMED_NULL != fmed_getval("total_samples"))
		fmed_setval("total_samples", u->until);

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

	if (FMED_NULL == (int64)(pos = fmed_getval("current_position")))
		return FMED_RDONE;

	if (u->asis) {
		if (pos >= u->until) {
			dbglog(core, d->trk, "until", "reached sample #%U", u->until);
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

	p->nch = fmed_getval("pcm_channels");
	if (p->nch > 2) {
		ffmem_free(p);
		return NULL;
	}

	p->do_crc = (1 == fmed_getval("pcm_crc"));
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
		fmed_setval("conv_pcm_ileaved", 0);
		fmed_setval("conv_pcm_format", FFPCM_16LE);
		p->state = 1;
		return FMED_RMORE;

	case 1:
		if (1 == fmed_getval("pcm_ileaved")
			|| FFPCM_16LE != fmed_getval("pcm_format")) {
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
	p->fmt.format = fmed_getval("pcm_format");
	p->fmt.channels = fmed_getval("pcm_channels");
	p->fmt.sample_rate = fmed_getval("pcm_sample_rate");
	p->fmt.ileaved = (1 == fmed_getval("pcm_ileaved"));
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
	fmed_setval("pcm_peak", (uint)(db * 100));

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}
