/** OSS input/output.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>


static const fmed_core *core;

typedef struct oss_mod {
	fftmrq_entry tmr;
	ffaudio_buf *out;
	ffpcm fmt;
	audio_out *usedby;
	const fmed_track *track;
	uint dev_idx;
} oss_mod;

static oss_mod *mod;

enum { I_TRYOPEN, I_OPEN, I_DATA };

static struct oss_out_conf_t {
	uint idev;
	uint buflen;
} oss_out_conf;

//FMEDIA MODULE
static const void* oss_iface(const char *name);
static int oss_conf(const char *name, fmed_conf_ctx *ctx);
static int oss_sig(uint signo);
static void oss_destroy(void);
static const fmed_mod fmed_oss_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &oss_iface,
	.sig = &oss_sig,
	.destroy = &oss_destroy,
	.conf = &oss_conf,
};

static int oss_create(audio_out *a, fmed_filt *d);

//OUTPUT
static void* oss_open(fmed_filt *d);
static int oss_write(void *ctx, fmed_filt *d);
static void oss_close(void *ctx);
static int oss_out_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_oss_out = {
	&oss_open, &oss_write, &oss_close
};

static const fmed_conf_arg oss_out_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct oss_out_conf_t, idev) },
	{ "buffer_length",	FMC_INT32,  FMC_O(struct oss_out_conf_t, buflen) },
};

//INPUT
static void* oss_in_open(fmed_filt *d);
static void oss_in_close(void *ctx);
static int oss_in_read(void *ctx, fmed_filt *d);
static const fmed_filter fmed_oss_in = {
	&oss_in_open, &oss_in_read, &oss_in_close
};

//ADEV
static int oss_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_oss_adev = {
	.list = &oss_adev_list,
	.listfree = &audio_dev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_oss_mod;
}


static const void* oss_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_oss_out;
	} else if (!ffsz_cmp(name, "in")) {
		return &fmed_oss_in;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_oss_adev;
	}
	return NULL;
}

static int oss_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return oss_out_config(ctx);
	return -1;
}

static int oss_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_new(oss_mod)))
			return -1;

		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void oss_destroy(void)
{
	if (mod == NULL)
		return;

	ffoss.free(mod->out);
	ffoss.uninit();
	ffmem_free(mod);
	mod = NULL;
}

static int mod_init(fmed_trk *trk)
{
	return 0;
}

static int oss_adev_list(fmed_adev_ent **ents, uint flags)
{
	if (0 != mod_init(NULL))
		return -1;

	int r;
	if (0 > (r = audio_dev_list(core, &ffoss, ents, flags, "oss")))
		return -1;
	return r;
}

static int oss_out_config(fmed_conf_ctx *ctx)
{
	oss_out_conf.idev = 0;
	oss_out_conf.buflen = 500;
	fmed_conf_addctx(ctx, &oss_out_conf, oss_out_conf_args);
	return 0;
}

static void* oss_open(fmed_filt *d)
{
	audio_out *a;

	if (0 != mod_init(d->trk))
		return NULL;

	if (NULL == (a = ffmem_new(audio_out)))
		return NULL;
	a->core = core;
	a->audio = &ffoss;
	a->track = mod->track;
	a->trk = d->trk;
	return a;
}

static void oss_close(void *ctx)
{
	audio_out *a = ctx;

	if (mod->usedby == a) {
		if (FMED_NULL != mod->track->getval(a->trk, "stopped")) {
			ffoss.free(mod->out);
			mod->out = NULL;

		} else {
			if (0 != ffoss.stop(mod->out))
				errlog(core, a->trk, "oss", "stop: %s", ffoss.error(mod->out));
			ffoss.clear(mod->out);
		}

		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
	}

	ffoss.dev_free(a->dev);
	ffmem_free(a);
}

static int oss_create(audio_out *a, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(a->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		a->dev_idx = oss_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	a->buffer_length_msec = oss_out_conf.buflen;

	if (mod->out != NULL) {

		audio_out *cur = mod->usedby;
		if (cur != NULL) {
			mod->usedby = NULL;
			core->timer(&mod->tmr, 0, 0);
			audio_out_onplay(cur);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& fmt.sample_rate == mod->fmt.sample_rate
			&& a->dev_idx == mod->dev_idx) {

			ffoss.stop(mod->out);
			ffoss.clear(mod->out);
			a->stream = mod->out;

			ffoss.dev_free(a->dev);
			a->dev = NULL;

			reused = 1;
			goto fin;
		}

		ffoss.free(mod->out);
		mod->out = NULL;
	}

	a->try_open = (a->state == I_TRYOPEN);
	r = audio_out_open(a, d, &fmt);
	if (r == FMED_RMORE) {
		a->state = I_OPEN;
		return FMED_RMORE;
	} else if (r != FMED_ROK)
		return r;

	ffoss.dev_free(a->dev);
	a->dev = NULL;

	mod->out = a->stream;
	mod->fmt = fmt;
	mod->dev_idx = a->dev_idx;

fin:
	dbglog(core, d->trk, "oss", "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", a->buffer_length_msec
		, fmt.sample_rate);

	mod->usedby = a;

	mod->tmr.handler = audio_out_onplay;
	mod->tmr.param = a;
	if (0 != core->timer(&mod->tmr, a->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;
}

static int oss_write(void *ctx, fmed_filt *d)
{
	audio_out *a = ctx;
	int r;

	switch (a->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough
	case I_OPEN:
		if (0 != (r = oss_create(a, d)))
			return r;
		a->state = I_DATA;
		return FMED_RMORE;

	case I_DATA:
		break;
	}

	if (mod->usedby != a || (d->flags & FMED_FSTOP)) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = audio_out_write(a, d);
	if (r == FMED_RERR) {
		ffoss.free(mod->out);
		mod->out = NULL;
		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
		return FMED_RERR;
	}
	return r;
}


typedef struct oss_in {
	audio_in in;
	fftmrq_entry tmr;
} oss_in;

static void* oss_in_open(fmed_filt *d)
{
	if (0 != mod_init(d->trk))
		return NULL;

	oss_in *pi = ffmem_new(oss_in);
	audio_in *a = &pi->in;
	a->core = core;
	a->audio = &ffoss;
	a->track = mod->track;
	a->trk = d->trk;

	int idx;
	if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "capture_device"))) {
		// use device specified by user
		a->dev_idx = idx;
	} else {
		a->dev_idx = 0;
	}

	a->buffer_length_msec = 0;

	if (0 != audio_in_open(a, d))
		goto fail;

	pi->tmr.handler = audio_oncapt;
	pi->tmr.param = a;
	if (0 != core->timer(&pi->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return pi;

fail:
	oss_in_close(pi);
	return NULL;
}

static void oss_in_close(void *ctx)
{
	oss_in *pi = ctx;
	core->timer(&pi->tmr, 0, 0);
	audio_in_close(&pi->in);
	ffmem_free(pi);
}

static int oss_in_read(void *ctx, fmed_filt *d)
{
	oss_in *pi = ctx;
	return audio_in_read(&pi->in, d);
}
