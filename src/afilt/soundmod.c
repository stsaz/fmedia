/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/ring.h>


static const fmed_core *core;

typedef struct sndmod_conv {
	uint state;
	uint out_samp_size;
	ffpcmex inpcm
		, outpcm;
	ffstr3 buf;
	uint off;
} sndmod_conv;

enum {
	CONV_OUTBUF_MSEC = 500,
	SILGEN_BUF_MSEC = 100,
};


//FMEDIA MODULE
static const void* sndmod_iface(const char *name);
static int sndmod_sig(uint signo);
static void sndmod_destroy(void);
static const fmed_mod fmed_sndmod_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&sndmod_iface, &sndmod_sig, &sndmod_destroy
};

//CONVERTER
static void* sndmod_conv_open(fmed_filt *d);
static int sndmod_conv_process(void *ctx, fmed_filt *d);
static void sndmod_conv_close(void *ctx);
static ssize_t sndmod_conv_cmd(void *ctx, uint cmd, ...);
static const struct fmed_filter2 fmed_sndmod_conv = {
	&sndmod_conv_open, &sndmod_conv_process, &sndmod_conv_close, &sndmod_conv_cmd
};

//AUTO-CONVERTER
static void* autoconv_open(fmed_filt *d);
static void autoconv_close(void *ctx);
static int autoconv_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_sndmod_autoconv = {
	&autoconv_open, &autoconv_process, &autoconv_close
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

//SILENCE GEN
static void* silgen_open(fmed_filt *d);
static void silgen_close(void *ctx);
static int silgen_process(void *ctx, fmed_filt *d);
static const fmed_filter sndmod_silgen = { &silgen_open, &silgen_process, &silgen_close };

//MEMBUF
static void* membuf_open(fmed_filt *d);
static void membuf_close(void *ctx);
static int membuf_write(void *ctx, fmed_filt *d);
static const struct fmed_filter sndmod_membuf = {
	&membuf_open, &membuf_write, &membuf_close
};


const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_sndmod_mod;
}


struct submod {
	const char *name;
	const fmed_filter *iface;
};

static const struct submod submods[] = {
	{ "conv", (fmed_filter*)&fmed_sndmod_conv },
	{ "autoconv", &fmed_sndmod_autoconv },
	{ "gain", &fmed_sndmod_gain },
	{ "until", &fmed_sndmod_until },
	{ "peaks", &fmed_sndmod_peaks },
	{ "rtpeak", &fmed_sndmod_rtpeak },
	{ "silgen", &sndmod_silgen },
	{ "membuf", &sndmod_membuf },
};

static const void* sndmod_iface(const char *name)
{
	const struct submod *m;
	FFARRS_FOREACH(submods, m) {
		if (ffsz_eq(name, m->name))
			return m->iface;
	}
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

static ssize_t sndmod_conv_cmd(void *ctx, uint cmd, ...)
{
	sndmod_conv *c = ctx;
	va_list va;
	va_start(va, cmd);
	ssize_t r = -1;

	switch (cmd) {
	case 0: {
		const struct fmed_aconv *conf = va_arg(va, void*);
		c->inpcm = conf->in;
		c->outpcm = conf->out;
		c->state = 1;
		r = 0;
		break;
	}
	}

	va_end(va);
	return r;
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
		, ffpcm_fmtstr(in->format), in->channels, in->sample_rate, (in->ileaved) ? "i" : "ni"
		, ffpcm_fmtstr(out->format), out->channels & FFPCM_CHMASK, out->sample_rate, (out->ileaved) ? "i" : "ni");
}

static int sndmod_conv_prepare(sndmod_conv *c, fmed_filt *d)
{
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

	return FMED_ROK;
}

static int sndmod_conv_process(void *ctx, fmed_filt *d)
{
	sndmod_conv *c = ctx;
	uint samples;
	int r;

	switch (c->state) {
	case 0:
		return FMED_RERR; // settings are empty
	case 1:
		r = sndmod_conv_prepare(c, d);
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


/* Audio converter that is automatically added into chain when track is created.
The filter is initialized in 2 steps:

1. The first time the converter is called, it just sets output audio format and returns with no data.
The conversion format may be already set by previous filters - the converter preserves those settings.
The next filters in chain may set the format they need, and then they ask for actual audio data.

2. The second time the converter is called, it initializes #soundmod.conv filter if needed, and then deletes itself from chain.
*/

struct autoconv {
	uint state;
	ffpcmex inpcm, outpcm;
};

static void* autoconv_open(fmed_filt *d)
{
	if (d->stream_copy) {
		if (ffsz_eq(d->datatype, "pcm")) {
			errlog(core, d->trk, "#soundmod.autoconv", "decoder doesn't support --stream-copy", 0);
			return NULL;
		}
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
		warnlog(core, d->trk, NULL, "conversion format was overwritten by output filters: %s/%u/%u"
			, ffpcm_fmtstr(out->format), out->channels, out->sample_rate);

	if (in->format == out->format
		&& in->channels == out->channels
		&& in->sample_rate == out->sample_rate
		&& in->ileaved == out->ileaved) {
		d->out = d->data,  d->outlen = d->datalen;
		return FMED_RDONE; //no conversion is needed
	}

	const struct fmed_filter2 *conv = core->getmod("#soundmod.conv");
	void *f = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "#soundmod.conv");
	if (f == NULL)
		return FMED_RERR;
	void *fi = (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_INSTANCE, f);
	if (fi == NULL)
		return FMED_RERR;

	struct fmed_aconv conf = {};
	conf.in = *in;
	conf.out = *out;
	conv->cmd(fi, 0, &conf);

	d->out = d->data,  d->outlen = d->datalen;
	return FMED_RDONE;
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

	if (d->stream_copy) {
		if (d->audio.pos >= u->until) {
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


struct silgen {
	uint state;
	void *buf;
	size_t cap;
};

static void* silgen_open(fmed_filt *d)
{
	struct silgen *c;
	if (NULL == (c = ffmem_new(struct silgen)))
		return NULL;
	return c;
}

static void silgen_close(void *ctx)
{
	struct silgen *c = ctx;
	ffmem_safefree(c->buf);
	ffmem_free(c);
}

static int silgen_process(void *ctx, fmed_filt *d)
{
	struct silgen *c = ctx;
	switch (c->state) {

	case 0:
		d->audio.convfmt = d->audio.fmt;
		d->datatype = "pcm";
		c->state = 1;
		return FMED_RDATA;

	case 1:
		c->cap = ffpcm_bytes(&d->audio.convfmt, SILGEN_BUF_MSEC);
		if (NULL == (c->buf = ffmem_alloc(c->cap)))
			return FMED_RSYSERR;
		ffmem_zero(c->buf, c->cap);
		c->state = 2;
		// fall through

	case 2:
		break;
	}

	d->out = c->buf,  d->outlen = c->cap;
	return FMED_RDATA;
}


struct membuf {
	ffringbuf buf;
	size_t size;
};

static void* membuf_open(fmed_filt *d)
{
	if (!d->audio.fmt.ileaved) {
		errlog(core, d->trk, "#soundmod.membuf", "non-interleaved audio format isn't supported");
		return NULL;
	}

	struct membuf *m = ffmem_new(struct membuf);
	if (m == NULL)
		return NULL;

	size_t size = ffpcm_bytes(&d->audio.fmt, d->a_prebuffer);
	m->size = size;
	size = ff_align_power2(size + 1);
	void *p = ffmem_alloc(size);
	if (p == NULL) {
		ffmem_free(m);
		return NULL;
	}
	ffringbuf_init(&m->buf, p, size);
	return m;
}

static void membuf_close(void *ctx)
{
	struct membuf *m = ctx;
	ffmem_free(ffringbuf_data(&m->buf));
}

static int membuf_write(void *ctx, fmed_filt *d)
{
	struct membuf *m = ctx;

	if (d->save_trk) {
		ffstr s;
		ffringbuf_readptr(&m->buf, &s, m->size);
		d->out = s.ptr,  d->outlen = s.len;
		return (s.len == 0) ? FMED_RDONE : FMED_RDATA;
	}

	if (d->flags & FMED_FSTOP)
		return FMED_RFIN;

	ffringbuf_overwrite(&m->buf, d->data, d->datalen);
	return FMED_RMORE;
}
