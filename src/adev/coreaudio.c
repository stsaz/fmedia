/** CoreAudio input/output.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>


#undef errlog
#undef dbglog
#define errlog(...)  fmed_errlog(core, NULL, "coraud", __VA_ARGS__)
#define dbglog(...)  fmed_dbglog(core, NULL, "coraud", __VA_ARGS__)


static const fmed_core *core;
static const fmed_track *track;

//FMEDIA MODULE
static const void* coraud_iface(const char *name);
static int coraud_conf(const char *name, fmed_conf_ctx *ctx);
static int coraud_sig(uint signo);
static void coraud_destroy(void);
static const fmed_mod fmed_coraud_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &coraud_iface,
	.sig = &coraud_sig,
	.destroy = &coraud_destroy,
	.conf = &coraud_conf,
};

//OUTPUT
static void* coraud_open(fmed_filt *d);
static int coraud_write(void *ctx, fmed_filt *d);
static void coraud_close(void *ctx);
static int coraud_out_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_coraud_out = {
	&coraud_open, &coraud_write, &coraud_close
};

struct coraud_out_conf_t {
	uint idev;
	uint buflen;
};
static struct coraud_out_conf_t coraud_out_conf;

static const fmed_conf_arg coraud_out_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct coraud_out_conf_t, idev) },
};


//INPUT
static void* coraud_in_open(fmed_filt *d);
static int coraud_in_read(void *ctx, fmed_filt *d);
static void coraud_in_close(void *ctx);
static int coraud_in_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_coraud_in = {
	&coraud_in_open, &coraud_in_read, &coraud_in_close
};

struct coraud_in_conf_t {
	uint idev;
	uint buflen;
};
static struct coraud_in_conf_t coraud_in_conf;

static const fmed_conf_arg coraud_in_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct coraud_in_conf_t, idev) },
	{ "buffer_length",	FMC_INT32NZ,  FMC_O(struct coraud_in_conf_t, buflen) },
};


//ADEV
static int coraud_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_coraud_adev = {
	.list = &coraud_adev_list,
	.listfree = &audio_dev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_coraud_mod;
}


static const void* coraud_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_coraud_out;
	} else if (!ffsz_cmp(name, "in")) {
		return &fmed_coraud_in;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_coraud_adev;
	}
	return NULL;
}

static int coraud_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return coraud_out_config(ctx);
	else if (!ffsz_cmp(name, "in"))
		return coraud_in_config(ctx);
	return -1;
}

static int coraud_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		fflk_setup();
		return 0;

	case FMED_OPEN:
		track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void coraud_destroy(void)
{
}

static int mod_init(fmed_trk *trk)
{
	static int init_ok;
	if (init_ok)
		return 0;

	ffaudio_init_conf conf = {};
	conf.app_name = "fmedia";
	if (0 != ffcoreaudio.init(&conf)) {
		errlog("init: %s", conf.error);
		return -1;
	}

	init_ok = 1;
	return 0;
}

static int coraud_adev_list(fmed_adev_ent **ents, uint flags)
{
	if (0 != mod_init(NULL))
		return -1;

	int r;
	if (0 > (r = audio_dev_list(core, &ffcoreaudio, ents, flags, "coreaud")))
		return -1;
	return r;
}


static int coraud_out_config(fmed_conf_ctx *ctx)
{
	coraud_out_conf.idev = 0;
	fmed_conf_addctx(ctx, &coraud_out_conf, coraud_out_conf_args);
	return 0;
}

struct coraud_out {
	uint state;
	audio_out out;
	fftmrq_entry tmr;
};

static void* coraud_open(fmed_filt *d)
{
	if (0 != mod_init(d->trk))
		return NULL;

	struct coraud_out *c;
	if (NULL == (c = ffmem_new(struct coraud_out)))
		return NULL;
	c->out.trk = d->trk;
	c->out.core = core;
	c->out.audio = &ffcoreaudio;
	c->out.track = d->track;
	c->out.trk = d->trk;
	return c;
}

static void coraud_close(void *ctx)
{
	struct coraud_out *c = ctx;
	core->timer(&c->tmr, 0, 0);
	ffcoreaudio.free(c->out.stream);
	ffcoreaudio.dev_free(c->out.dev);
	ffmem_free(c);
}

enum { I_TRYOPEN, I_OPEN, I_DATA };

static int coraud_create(struct coraud_out *c, fmed_filt *d)
{
	audio_out *a = &c->out;
	ffpcm fmt;
	int r;

	if (FMED_NULL == (int)(a->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		a->dev_idx = coraud_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	a->buffer_length_msec = coraud_out_conf.buflen;

	a->try_open = (a->state == I_TRYOPEN);
	r = audio_out_open(a, d, &fmt);
	if (r == FMED_RMORE) {
		a->state = I_OPEN;
		return FMED_RMORE;
	} else if (r != FMED_ROK)
		return r;

	ffcoreaudio.dev_free(a->dev);
	a->dev = NULL;

	dbglog("%s buffer %ums, %uHz"
		, "opened", a->buffer_length_msec
		, fmt.sample_rate);

	c->tmr.handler = audio_out_onplay;
	c->tmr.param = a;
	if (0 != core->timer(&c->tmr, a->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;
}

static int coraud_write(void *ctx, fmed_filt *d)
{
	struct coraud_out *c = ctx;
	int r;

	switch (c->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough

	case I_OPEN:
		if (0 != (r = coraud_create(c, d)))
			return r;
		c->state = I_DATA;
		break;

	case I_DATA:
		break;
	}

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = audio_out_write(&c->out, d);
	if (r == FMED_RERR) {
		core->timer(&c->tmr, 0, 0);
		return FMED_RERR;
	}
	return r;
}


static int coraud_in_config(fmed_conf_ctx *ctx)
{
	coraud_in_conf.idev = 0;
	coraud_in_conf.buflen = 1000;
	fmed_conf_addctx(ctx, &coraud_in_conf, coraud_in_conf_args);
	return 0;
}

struct coraud_in {
	audio_in in;
	fftmrq_entry tmr;
};

static void* coraud_in_open(fmed_filt *d)
{
	if (0 != mod_init(d->trk))
		return NULL;

	struct coraud_in *c = ffmem_new(struct coraud_in);
	audio_in *a = &c->in;
	a->core = core;
	a->audio = &ffcoreaudio;
	a->track = d->track;
	a->trk = d->trk;

	int idx;
	if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "capture_device"))) {
		// use device specified by user
		a->dev_idx = idx;
	} else {
		a->dev_idx = coraud_in_conf.idev;
	}

	a->buffer_length_msec = coraud_in_conf.buflen;

	if (0 != audio_in_open(a, d))
		goto fail;

	c->tmr.handler = audio_oncapt;
	c->tmr.param = a;
	if (0 != core->timer(&c->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return c;

fail:
	coraud_in_close(c);
	return NULL;
}

static void coraud_in_close(void *ctx)
{
	struct coraud_in *c = ctx;
	core->timer(&c->tmr, 0, 0);
	audio_in_close(&c->in);
	ffmem_free(c);
}

static int coraud_in_read(void *ctx, fmed_filt *d)
{
	struct coraud_in *c = ctx;
	return audio_in_read(&c->in, d);
}
