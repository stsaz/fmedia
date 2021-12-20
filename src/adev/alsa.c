/** ALSA input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>


static const fmed_core *core;

typedef struct alsa_mod {
	ffaudio_buf *out;
	fftmrq_entry tmr;
	ffpcm fmt;
	audio_out *usedby;
	const fmed_track *track;
	uint dev_idx;
	uint init_ok :1;
} alsa_mod;

static alsa_mod *mod;

enum { I_TRYOPEN, I_OPEN, I_DATA };

static struct alsa_out_conf_t {
	uint idev;
	uint buflen;
	uint nfy_rate;
} alsa_out_conf;

//FMEDIA MODULE
static const void* alsa_iface(const char *name);
static int alsa_conf(const char *name, fmed_conf_ctx *ctx);
static int alsa_sig(uint signo);
static void alsa_destroy(void);
static const fmed_mod fmed_alsa_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&alsa_iface, &alsa_sig, &alsa_destroy, &alsa_conf
};

static int alsa_init(fmed_trk *trk);
static int alsa_create(audio_out *a, fmed_filt *d);

//OUTPUT
static void* alsa_open(fmed_filt *d);
static int alsa_write(void *ctx, fmed_filt *d);
static void alsa_close(void *ctx);
static int alsa_out_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_alsa_out = {
	&alsa_open, &alsa_write, &alsa_close
};

static const fmed_conf_arg alsa_out_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct alsa_out_conf_t, idev) },
	{ "buffer_length",	FMC_INT32NZ,  FMC_O(struct alsa_out_conf_t, buflen) },
	{ "notify_rate",	FMC_INT32,  FMC_O(struct alsa_out_conf_t, nfy_rate) },
};

//INPUT
static void* alsa_in_open(fmed_filt *d);
static void alsa_in_close(void *ctx);
static int alsa_in_read(void *ctx, fmed_filt *d);
static int alsa_in_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_alsa_in = {
	&alsa_in_open, &alsa_in_read, &alsa_in_close
};

static struct alsa_in_conf_t {
	uint idev;
	uint buflen;
} alsa_in_conf;

static const fmed_conf_arg alsa_in_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct alsa_in_conf_t, idev) },
	{ "buffer_length",	FMC_INT32NZ,  FMC_O(struct alsa_in_conf_t, buflen) },
};

//ADEV
static int alsa_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_alsa_adev = {
	.list = &alsa_adev_list,
	.listfree = audio_dev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_alsa_mod;
}


static const void* alsa_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_alsa_out;
	} else if (!ffsz_cmp(name, "in")) {
		return &fmed_alsa_in;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_alsa_adev;
	}
	return NULL;
}

static int alsa_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return alsa_out_config(ctx);
	else if (!ffsz_cmp(name, "in"))
		return alsa_in_config(ctx);
	return -1;
}

static int alsa_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_tcalloc1(alsa_mod)))
			return -1;

		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void alsa_destroy(void)
{
	if (mod != NULL) {
		if (mod->out != NULL)
			ffalsa.free(mod->out);
		ffmem_free(mod);
		mod = NULL;
	}

	ffalsa.uninit(core->kq);
}

static int alsa_init(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;

	ffaudio_init_conf conf = {};
	if (0 != ffalsa.init(&conf)) {
		errlog(core, trk, "alsa", "init: %s", conf.error);
		return -1;
	}
	mod->init_ok = 1;
	return 0;
}


static int alsa_adev_list(fmed_adev_ent **ents, uint flags)
{
	int r;
	if (0 > (r = audio_dev_list(core, &ffalsa, ents, flags, "alsa")))
		return -1;
	return r;
}


static int alsa_out_config(fmed_conf_ctx *ctx)
{
	alsa_out_conf.idev = 0;
	alsa_out_conf.buflen = 500;
	alsa_out_conf.nfy_rate = 0;
	fmed_conf_addctx(ctx, &alsa_out_conf, alsa_out_conf_args);
	return 0;
}

static void* alsa_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, "alsa", "unsupported input data type: %s", d->datatype);
		return NULL;
	}

	if (0 != alsa_init(d->trk))
		return NULL;

	audio_out *a;
	if (NULL == (a = ffmem_new(audio_out)))
		return NULL;
	a->core = core;
	a->track = mod->track;
	a->audio = &ffalsa;
	a->trk = d->trk;
	return a;
}

static void alsa_close(void *ctx)
{
	audio_out *a = ctx;
	if (mod->usedby == a) {
		if (FMED_NULL != mod->track->getval(a->trk, "stopped")) {
			ffalsa.free(mod->out);
			mod->out = NULL;

		} else {
			if (0 != ffalsa.stop(mod->out))
				errlog(core, a->trk,  "alsa", "stop(): %s", ffalsa.error(mod->out));
			ffalsa.clear(mod->out);
		}

		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
	}

	ffalsa.dev_free(a->dev);
	ffmem_free(a);
}

static int alsa_create(audio_out *a, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(a->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		a->dev_idx = alsa_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	a->buffer_length_msec = alsa_out_conf.buflen;
	a->aflags = FFAUDIO_O_HWDEV; // try "hw" device first, then fall back to "plughw"

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
			&& mod->dev_idx == a->dev_idx) {

			ffalsa.stop(mod->out);
			ffalsa.clear(mod->out);
			a->stream = mod->out;

			ffalsa.dev_free(a->dev);
			a->dev = NULL;

			reused = 1;
			goto fin;
		}

		ffalsa.free(mod->out);
		mod->out = NULL;
	}

	a->try_open = (a->state == I_TRYOPEN);
	r = audio_out_open(a, d, &fmt);
	if (r == FMED_RMORE) {
		a->state = I_OPEN;
		return FMED_RMORE;
	} else if (r != FMED_ROK)
		return r;

	ffalsa.dev_free(a->dev);
	a->dev = NULL;

	mod->out = a->stream;
	mod->fmt = fmt;
	mod->dev_idx = a->dev_idx;

fin:
	mod->usedby = a;
	dbglog(core, d->trk, "alsa", "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", a->buffer_length_msec
		, mod->fmt.sample_rate);

	// if (alsa_out_conf.nfy_rate != 0)
	// 	mod->out.nfy_interval = ffpcm_samples(alsa_out_conf.buflen / alsa_out_conf.nfy_rate, fmt.sample_rate);
	mod->tmr.handler = audio_out_onplay;
	mod->tmr.param = a;
	if (0 != core->timer(&mod->tmr, a->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;

}

static int alsa_write(void *ctx, fmed_filt *d)
{
	audio_out *a = ctx;
	int r;

	switch (a->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough
	case I_OPEN:
		if (0 != (r = alsa_create(a, d)))
			return r;
		a->state = I_DATA;
		break;

	case I_DATA:
		break;
	}

	if (mod->usedby != a || (d->flags & FMED_FSTOP)) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = audio_out_write(a, d);
	if (r == FMED_RERR) {
		ffalsa.free(mod->out);
		mod->out = NULL;
		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
		return FMED_RERR;
	}
	return r;
}


typedef struct alsa_in {
	audio_in in;
	fftmrq_entry tmr;
} alsa_in;

static int alsa_in_config(fmed_conf_ctx *ctx)
{
	alsa_in_conf.idev = 0;
	alsa_in_conf.buflen = 500;
	fmed_conf_addctx(ctx, &alsa_in_conf, alsa_in_conf_args);
	return 0;
}

static void* alsa_in_open(fmed_filt *d)
{
	if (0 != alsa_init(d->trk))
		return NULL;

	alsa_in *al = ffmem_new(alsa_in);
	audio_in *a = &al->in;
	a->core = core;
	a->audio = &ffalsa;
	a->track = mod->track;
	a->trk = d->trk;

	int idx;
	if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "capture_device"))) {
		// use device specified by user
		a->dev_idx = idx;
	} else {
		a->dev_idx = 0;
	}

	a->buffer_length_msec = alsa_in_conf.buflen;

	if (0 != audio_in_open(a, d))
		goto fail;

	al->tmr.handler = audio_oncapt;
	al->tmr.param = a;
	if (0 != core->timer(&al->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return al;

fail:
	alsa_in_close(al);
	return NULL;
}

static void alsa_in_close(void *ctx)
{
	alsa_in *al = ctx;
	core->timer(&al->tmr, 0, 0);
	audio_in_close(&al->in);
	ffmem_free(al);
}

static int alsa_in_read(void *ctx, fmed_filt *d)
{
	alsa_in *al = ctx;
	return audio_in_read(&al->in, d);
}
