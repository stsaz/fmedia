/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/crc.h>
#include <FF/ring.h>


#define infolog(trk, ...)  fmed_infolog(core, trk, FILT_NAME, __VA_ARGS__)


const fmed_core *core;

enum {
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

//START-LEVEL
static void* startlev_open(fmed_filt *d);
static void startlev_close(void *ctx);
static int startlev_process(void *ctx, fmed_filt *d);
static const struct fmed_filter sndmod_startlev = {
	&startlev_open, &startlev_process, &startlev_close
};

//STOP-LEVEL
static void* stoplev_open(fmed_filt *d);
static void stoplev_close(void *ctx);
static int stoplev_process(void *ctx, fmed_filt *d);
static const struct fmed_filter sndmod_stoplev = {
	&stoplev_open, &stoplev_process, &stoplev_close
};

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

extern const struct fmed_filter2 fmed_sndmod_conv;
extern const fmed_filter fmed_sndmod_autoconv;
extern const fmed_filter fmed_sndmod_split;

static const struct submod submods[] = {
	{ "conv", (fmed_filter*)&fmed_sndmod_conv },
	{ "autoconv", &fmed_sndmod_autoconv },
	{ "gain", &fmed_sndmod_gain },
	{ "until", &fmed_sndmod_until },
	{ "split", &fmed_sndmod_split },
	{ "peaks", &fmed_sndmod_peaks },
	{ "rtpeak", &fmed_sndmod_rtpeak },
	{ "silgen", &sndmod_silgen },
	{ "startlevel", &sndmod_startlev },
	{ "stoplevel", &sndmod_stoplev },
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
	dbglog(core, d->trk, "until", "at %U..%U", pos, pos + samps);
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
		ffstr_catfmt(&buf, FF_NEWLN "PCM peaks (%,U total samples):" FF_NEWLN
			, p->total);

		if (p->total != 0) {
			for (ich = 0;  ich != p->nch;  ich++) {

				double hi = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].high));
				double avg = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].sum / p->total));
				ffstr_catfmt(&buf, "Channel #%L: highest peak:%.2FdB, avg peak:%.2FdB.  Clipped: %U (%.4F%%).  CRC:%08xu" FF_NEWLN
					, ich + 1, hi, avg
					, p->ch[ich].clipped, ((double)p->ch[ich].clipped * 100 / p->total)
					, p->ch[ich].crc);
			}
		}

		core->log(FMED_LOG_USER, d->trk, NULL, "%S", &buf);
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
	double t;
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

	double maxpeak;
	ffpcm_peak(&p->fmt, d->data, d->datalen / ffpcm_size1(&p->fmt), &maxpeak);
	double db = ffpcm_gain2db(maxpeak);
	d->audio.maxpeak = db;
	dbglog(core, d->trk, "rtpeak", "maxpeak:%.2F", db);

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

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

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


#define FILT_NAME "soundmod.startlevel"

struct startlev {
	ffpcmex fmt;
	double level;
	double val;
	uint64 offset; //number of skipped samples
	void *ni[8];
};

static void* startlev_open(fmed_filt *d)
{
	if (d->audio.fmt.channels > 8)
		return NULL;
	struct startlev *c = ffmem_new(struct startlev);
	if (c == NULL)
		return NULL;
	c->fmt = d->audio.fmt;
	c->level = ffpcm_db2gain(-d->a_start_level);
	return c;
}

static void startlev_close(void *ctx)
{
	struct startlev *c = ctx;
	ffmem_free(c);
}

static int startlev_cb(void *ctx, double val)
{
	struct startlev *c = ctx;
	if (val > c->level) {
		c->val = val;
		return 1;
	}
	return 0;
}

/** Skip audio until the signal level becomes loud enough, and then exit. */
static int startlev_process(void *ctx, fmed_filt *d)
{
	struct startlev *c = ctx;
	size_t samples = d->datalen / ffpcm_size1(&c->fmt);
	ssize_t r = ffpcm_process(&c->fmt, d->data, samples, &startlev_cb, c);
	if (r == -1) {
		c->offset += samples;
		return FMED_RMORE;
	} else if (r < 0) {
		return FMED_RERR;
	}
	c->offset += r;

	double db = ffpcm_gain2db(c->val);
	uint64 tms = ffpcm_time(c->offset, c->fmt.sample_rate);
	infolog(d->trk, "found %.2FdB peak at %u:%02u.%03u (%,U samples)"
		, db, (uint)((tms / 1000) / 60), (uint)((tms / 1000) % 60), (uint)(tms % 1000), c->offset);

	if (c->fmt.ileaved) {
		d->out = (void*)(d->data + r * ffpcm_size1(&c->fmt));
	} else {
		for (uint i = 0;  i != c->fmt.channels;  i++) {
			c->ni[i] = (char*)d->datani[i] + r * ffpcm_bits(c->fmt.format) / 8;
		}
		d->outni = c->ni;
	}
	d->outlen = d->datalen - r * ffpcm_size1(&c->fmt);
	return FMED_RDONE;
}

#undef FILT_NAME


#define FILT_NAME "soundmod.stoplevel"

struct stoplev {
	ffpcmex fmt;
	uint state;
	uint all_samples;
	uint max_samples;
	uint min_stop_samples;
	uint nsamples;
	double level;
	double val;
};

#define STOPLEV_DEF_TIME  5000

static void* stoplev_open(fmed_filt *d)
{
	struct stoplev *c = ffmem_new(struct stoplev);
	if (c == NULL)
		return NULL;
	c->fmt = d->audio.fmt;
	c->level = ffpcm_db2gain(-d->a_stop_level);
	uint t = (d->a_stop_level_time != 0) ? d->a_stop_level_time : STOPLEV_DEF_TIME;
	c->max_samples = c->fmt.channels * ffpcm_samples(t, c->fmt.sample_rate);
	c->min_stop_samples = (d->a_stop_level_mintime != 0)
		? c->fmt.channels * ffpcm_samples(d->a_stop_level_mintime, c->fmt.sample_rate)
		: 0;
	return c;
}

static void stoplev_close(void *ctx)
{
	struct stoplev *c = ctx;
	ffmem_free(c);
}

static int stoplev_cb(void *ctx, double val)
{
	struct stoplev *c = ctx;

	c->all_samples++;

	switch (c->state) {
	case 0:
		if (val > c->level)
			break;
		c->val = val;
		c->state = 1;
		//fallthrough

	case 1:
		if (val > c->level) {
			c->nsamples = 0;
			c->state = 0;
			break;
		}
		if (++c->nsamples != c->max_samples)
			break;
		c->state = 2;
		//fallthrough

	case 2:
		if (c->min_stop_samples == 0
			|| c->all_samples == c->min_stop_samples)
			return 1;
		break;
	}
	return 0;
}

static int stoplev_process(void *ctx, fmed_filt *d)
{
	struct stoplev *c = ctx;
	size_t samples = d->datalen / ffpcm_size1(&c->fmt);
	ssize_t r;

	if (!(d->flags & FMED_FFWD))
		return FMED_RMORE;

	r = ffpcm_process(&c->fmt, d->data, samples, &stoplev_cb, c);
	if (r == -1) {
		d->out = d->data;
		d->outlen = d->datalen;
		d->datalen = 0;
		return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RDATA;
	} else if (r < 0) {
		return FMED_RERR;
	}

	double db = ffpcm_gain2db(c->val);
	uint maxsamp = c->max_samples / c->fmt.channels;
	infolog(d->trk, "signal went below %.2FdB level (at %.2FdB) for %ums (%,L samples)"
		, (double)d->a_stop_level, db, (int)ffpcm_time(maxsamp, c->fmt.sample_rate), (size_t)maxsamp);

	d->out = d->data;
	d->outlen = r * ffpcm_size1(&c->fmt);
	return FMED_RLASTOUT;
}

#undef FILT_NAME


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
