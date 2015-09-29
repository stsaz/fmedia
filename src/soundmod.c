/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/crc.h>


static const fmed_core *core;

typedef struct sndmod_conv {
	uint state;
	ffpcmex inpcm
		, outpcm;
	ffstr3 buf;
} sndmod_conv;

enum {
	conf_bufsize = 64 * 1024
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
static const fmed_filter fmed_smod_until = {
	&sndmod_untl_open, &sndmod_untl_process, &sndmod_untl_close
};

//PEAKS
static void* sndmod_peaks_open(fmed_filt *d);
static int sndmod_peaks_process(void *ctx, fmed_filt *d);
static void sndmod_peaks_close(void *ctx);
static const fmed_filter fmed_sndmod_peaks = {
	&sndmod_peaks_open, &sndmod_peaks_process, &sndmod_peaks_close
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
	else if (!ffsz_cmp(name, "gain"))
		return &fmed_sndmod_gain;
	else if (!ffsz_cmp(name, "until"))
		return &fmed_smod_until;
	else if (!ffsz_cmp(name, "peaks"))
		return &fmed_sndmod_peaks;
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

	c->inpcm.format = (int)fmed_getval("pcm_format");
	c->inpcm.sample_rate = (int)fmed_getval("pcm_sample_rate");
	c->inpcm.channels = (int)fmed_getval("pcm_channels");
	if (1 == fmed_getval("pcm_ileaved"))
		c->inpcm.ileaved = 1;
	c->outpcm = c->inpcm;

	if (NULL == ffarr_alloc(&c->buf, conf_bufsize)) {
		ffmem_free(c);
		return NULL;
	}
	c->buf.len = conf_bufsize;

	return c;
}

static void sndmod_conv_close(void *ctx)
{
	sndmod_conv *c = ctx;
	ffarr_free(&c->buf);
	ffmem_free(c);
}

enum { CONV_CONF, CONV_CHK, CONV_DATA };

static int sndmod_conv_prepare(sndmod_conv *c, fmed_filt *d)
{
	int il, fmt;

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

	if ((c->state == CONV_CONF && (fmt == FMED_NULL || il == FMED_NULL))
		|| (c->state == CONV_CHK && !ffmemcmp(&c->outpcm, &c->inpcm, sizeof(ffpcmex)))) {

		if (c->state == CONV_CHK) {
			d->out = d->data;
			d->outlen = d->datalen;
			return FMED_RDONE; //the second call of the module - no conversion is needed
		}

		d->outlen = 0;
		c->state = CONV_CHK;
		return FMED_ROK;
	}

	dbglog(core, d->trk, "conv", "PCM conversion: %s/%u/%u/%s -> %s/%u/%u/%s"
		, ffpcm_fmtstr[c->inpcm.format], c->inpcm.channels, c->inpcm.sample_rate, (c->inpcm.ileaved) ? "i" : "ni"
		, ffpcm_fmtstr[c->outpcm.format], c->outpcm.channels, c->outpcm.sample_rate, (c->outpcm.ileaved) ? "i" : "ni");

	if (!c->outpcm.ileaved) {
		void **ni;
		char *data;
		size_t cap = c->buf.cap;
		uint i;
		if (NULL == ffarr_realloc(&c->buf, sizeof(void*) * c->outpcm.channels + c->buf.cap)) {
			return FMED_RERR;
		}
		ni = (void**)c->buf.ptr;
		data = c->buf.ptr + sizeof(void*) * c->outpcm.channels;
		for (i = 0;  i < c->outpcm.channels;  i++) {
			ni[i] = data + cap * i / c->outpcm.channels;
		}
	}

	c->buf.len /= ffpcm_size1(&c->outpcm);
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
		errlog(core, d->trk, "conv", "unsupported PCM conversion: %s/%u/%u/%s -> %s/%u/%u/%s"
			, ffpcm_fmtstr[c->inpcm.format], c->inpcm.channels, c->inpcm.sample_rate, (c->inpcm.ileaved) ? "i" : "ni"
			, ffpcm_fmtstr[c->outpcm.format], c->outpcm.channels, c->outpcm.sample_rate, (c->outpcm.ileaved) ? "i" : "ni");
		return FMED_RERR;
	}

	d->out = c->buf.ptr;
	d->outlen = samples * ffpcm_size1(&c->outpcm);
	d->datalen -= samples * ffpcm_size1(&c->inpcm);

	if (c->inpcm.ileaved)
		d->data += samples * ffpcm_size1(&c->inpcm);
	else
		ffarrp_shift((void**)d->datani, c->inpcm.channels, samples * ffpcm_size(c->inpcm.format, 1));

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
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
} sndmod_untl;

static void* sndmod_untl_open(fmed_filt *d)
{
	int64 val, rate;
	sndmod_untl *u;

	if (FMED_NULL == (val = fmed_getval("until_time")))
		return (void*)1;

	rate = fmed_getval("pcm_sample_rate");

	if (NULL == (u = ffmem_tcalloc1(sndmod_untl)))
		return NULL;
	if (val > 0)
		u->until = ffpcm_samples(val, rate);
	else
		u->until = -val * rate / 75;

	if (FMED_NULL != (val = fmed_getval("seek_time_abs"))) {
		if (val > 0)
			u->until -= ffpcm_samples(val, rate);
		else
			u->until -= -val * rate / 75;
	}

	val = fmed_getval("pcm_format");
	u->sampsize = ffpcm_size(val, fmed_getval("pcm_channels"));

	if (FMED_NULL != fmed_getval("total_samples"))
		fmed_setval("total_samples", u->until);
	return u;
}

static void sndmod_untl_close(void *ctx)
{
	sndmod_untl *u = ctx;
	if (ctx == (void*)1)
		return;
	ffmem_free(u);
}

static int sndmod_untl_process(void *ctx, fmed_filt *d)
{
	sndmod_untl *u = ctx;
	uint samps;
	uint64 pos;

	d->out = d->data;
	d->outlen = d->datalen;

	if ((d->flags & FMED_FLAST) || ctx == (void*)1)
		return FMED_RDONE;

	samps = d->datalen / u->sampsize;
	d->datalen = 0;
	pos = fmed_getval("current_position");
	if (pos + samps >= u->until) {
		dbglog(core, d->trk, "", "until_time is reached");
		d->outlen = (u->until - pos) * u->sampsize;
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
} sndmod_peaks;

static void* sndmod_peaks_open(fmed_filt *d)
{
	uint ich;
	sndmod_peaks *p = ffmem_tcalloc1(sndmod_peaks);
	if (p == NULL)
		return NULL;

	p->nch = fmed_getval("pcm_channels");
	if (p->nch > 2) {
		ffmem_free(p);
		return NULL;
	}

	if (1 == fmed_getval("pcm_crc")) {
		for (ich = 0;  ich != p->nch;  ich++) {
			p->ch[ich].crc = ffcrc32_start();
		}
	}
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

			if (p->ch[ich].high < sh)
				p->ch[ich].high = sh;

			p->ch[ich].sum += sh;
		}

		if (p->ch[ich].crc != 0)
			ffcrc32_updatestr(&p->ch[ich].crc, d->datani[ich], d->datalen / p->nch);
	}

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST) {
		ffstr3 buf = {0};
		ffstr_catfmt(&buf, "\nPCM peaks:\n");

		if (p->total != 0) {
			for (ich = 0;  ich != p->nch;  ich++) {

				if (p->ch[ich].crc != 0)
					ffcrc32_finish(&p->ch[ich].crc);

				ffstr_catfmt(&buf, "Channel #%L: highest peak:%04xu, avg peak:%04xu.  Clipped: %U (%.4F%%).  CRC:%08xu\n"
					, ich + 1, p->ch[ich].high, (int)(p->ch[ich].sum / p->total)
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
