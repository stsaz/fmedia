/** JACK input.
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>


#undef errlog
#undef dbglog
#define errlog(trk, ...)  fmed_errlog(core, trk, "jack", __VA_ARGS__)
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "jack", __VA_ARGS__)


static const fmed_core *core;

typedef struct jack_mod {
	const fmed_track *track;
	ffbool init_ok;
} jack_mod;
static jack_mod *mod;

//FMEDIA MODULE
static const void* jack_iface(const char *name);
static int jack_conf(const char *name, ffpars_ctx *ctx);
static int jack_sig(uint signo);
static void jack_destroy(void);
static const fmed_mod fmed_jack_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&jack_iface, &jack_sig, &jack_destroy, &jack_conf
};

//ADEV
static int jack_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_jack_adev = {
	.list = &jack_adev_list, .listfree = &audio_dev_listfree,
};

//INPUT
static void* jack_in_open(fmed_filt *d);
static int jack_in_read(void *ctx, fmed_filt *d);
static void jack_in_close(void *ctx);
static const fmed_filter fmed_jack_in = {
	&jack_in_open, &jack_in_read, &jack_in_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_jack_mod;
}


static const void* jack_iface(const char *name)
{
	if (ffsz_eq(name, "in"))
		return &fmed_jack_in;
	else if (ffsz_eq(name, "adev"))
		return &fmed_jack_adev;
	return NULL;
}

static int jack_conf(const char *name, ffpars_ctx *ctx)
{
	return -1;
}

static int jack_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		if (NULL == (mod = ffmem_new(jack_mod)))
			return -1;
		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void jack_destroy(void)
{
	ffjack.uninit();
}


static int jack_initonce(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;

	// A note for the user before using JACK library's functions
	fmed_infolog(core, NULL, "jack", "Note that the messages below may be printed by JACK library directly");

	ffaudio_init_conf conf = {};
	conf.app_name = "fmedia";
	if (0 != ffjack.init(&conf)) {
		errlog(trk, "init: %s", conf.error);
		return -1;
	}

	mod->init_ok = 1;
	return 0;
}

static int jack_adev_list(fmed_adev_ent **ents, uint flags)
{
	if (0 != jack_initonce(NULL))
		return -1;

	int r;
	if (0 > (r = audio_dev_list(core, &ffjack, ents, flags, "alsa")))
		return -1;
	return r;
}


typedef struct jack_in {
	audio_in in;
	fftmrq_entry tmr;
} jack_in;

static void* jack_in_open(fmed_filt *d)
{
	if (0 != jack_initonce(d->trk))
		return NULL;

	jack_in *ji = ffmem_new(jack_in);
	audio_in *a = &ji->in;
	a->core = core;
	a->audio = &ffjack;
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

	ji->tmr.handler = audio_oncapt;
	ji->tmr.param = a;
	if (0 != core->timer(&ji->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return ji;

fail:
	jack_in_close(ji);
	return NULL;
}

static void jack_in_close(void *ctx)
{
	jack_in *ji = ctx;
	core->timer(&ji->tmr, 0, 0);
	audio_in_close(&ji->in);
	ffmem_free(ji);
}

static int jack_in_read(void *ctx, fmed_filt *d)
{
	jack_in *ji = ctx;
	return audio_in_read(&ji->in, d);
}
