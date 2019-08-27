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

extern const struct fmed_filter2 fmed_sndmod_conv;
extern const fmed_filter fmed_sndmod_autoconv;
extern const fmed_filter fmed_sndmod_split;
extern const fmed_filter fmed_sndmod_peaks;
extern const fmed_filter sndmod_startlev;
extern const fmed_filter sndmod_stoplev;

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
