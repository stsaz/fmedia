/** Direct Sound input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>


static const fmed_core *core;
static const fmed_track *track;

static struct dsnd_out_conf_t {
	uint idev;
	uint buflen;
} dsnd_out_conf;

static struct dsnd_in_conf_t {
	uint idev;
	uint buflen;
} dsnd_in_conf;


//FMEDIA MODULE
static const void* dsnd_iface(const char *name);
static int dsnd_conf(const char *name, fmed_conf_ctx *ctx);
static int dsnd_sig(uint signo);
static void dsnd_destroy(void);
static const fmed_mod fmed_dsnd_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&dsnd_iface, &dsnd_sig, &dsnd_destroy, &dsnd_conf
};

//OUTPUT
static void* dsnd_open(fmed_filt *d);
static int dsnd_write(void *ctx, fmed_filt *d);
static void dsnd_close(void *ctx);
static int dsnd_out_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_dsnd_out = {
	&dsnd_open, &dsnd_write, &dsnd_close
};

static const fmed_conf_arg dsnd_out_conf_args[] = {
	{ "device_index",  FMC_INT32,  FMC_O(struct dsnd_out_conf_t, idev) }
	, { "buffer_length",  FMC_INT32NZ,  FMC_O(struct dsnd_out_conf_t, buflen) },
	{}
};

//INPUT
static void* dsnd_in_open(fmed_filt *d);
static int dsnd_in_read(void *ctx, fmed_filt *d);
static void dsnd_in_close(void *ctx);
static int dsnd_in_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_dsnd_in = {
	&dsnd_in_open, &dsnd_in_read, &dsnd_in_close
};

static const fmed_conf_arg dsnd_in_conf_args[] = {
	{ "device_index",  FMC_INT32,  FMC_O(struct dsnd_in_conf_t, idev) }
	, { "buffer_length",  FMC_INT32NZ,  FMC_O(struct dsnd_in_conf_t, buflen) },
	{}
};

//ADEV
static int dsnd_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_dsnd_adev = {
	.list = &dsnd_adev_list,
	.listfree = &audio_dev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_dsnd_mod;
}


static const void* dsnd_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_dsnd_out;
	} else if (!ffsz_cmp(name, "in")) {
		return &fmed_dsnd_in;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_dsnd_adev;
	}
	return NULL;
}

static int dsnd_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return dsnd_out_config(ctx);
	else if (!ffsz_cmp(name, "in"))
		return dsnd_in_config(ctx);
	return -1;
}

static int dsnd_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN: {
		ffaudio_init_conf conf = {};
		if (0 != ffdsound.init(&conf))
			return -1;
		track = core->getmod("#core.track");
		core->props->playback_dev_index = dsnd_out_conf.idev;
		return 0;
	}
	}
	return 0;
}

static void dsnd_destroy(void)
{
	ffdsound.uninit();
}


static int dsnd_adev_list(fmed_adev_ent **ents, uint flags)
{
	int r;
	if (0 > (r = audio_dev_list(core, &ffdsound, ents, flags, "dsound")))
		return -1;
	return r;
}


typedef struct dsnd_out {
	audio_out out;
	fftimerqueue_node tmr;
} dsnd_out;

static int dsnd_out_config(fmed_conf_ctx *ctx)
{
	dsnd_out_conf.idev = 0;
	dsnd_out_conf.buflen = 500;
	fmed_conf_addctx(ctx, &dsnd_out_conf, dsnd_out_conf_args);
	return 0;
}

static void* dsnd_open(fmed_filt *d)
{
	dsnd_out *ds;
	if (NULL == (ds = ffmem_new(dsnd_out)))
		return NULL;
	audio_out *a = &ds->out;
	a->core = core;
	a->audio = &ffdsound;
	a->track = track;
	a->trk = d->trk;
	return ds;
}

static void dsnd_close(void *ctx)
{
	dsnd_out *ds = ctx;
	ffdsound.dev_free(ds->out.dev);
	ffdsound.free(ds->out.stream);
	core->timer(&ds->tmr, 0, 0);
	ffmem_free(ds);
}

enum { I_TRYOPEN, I_OPEN, I_DATA };

static int dsnd_create(dsnd_out *ds, fmed_filt *d)
{
	audio_out *a = &ds->out;
	ffpcm fmt;
	int r;

	if (FMED_NULL == (int)(a->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		a->dev_idx = dsnd_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	a->buffer_length_msec = dsnd_out_conf.buflen;

	a->try_open = (a->state == I_TRYOPEN);
	r = audio_out_open(a, d, &fmt);
	if (r == FFAUDIO_EFORMAT) {
		a->state = I_OPEN;
		return FMED_RMORE;
	} else if (r != 0)
		return FMED_RERR;

	ffdsound.dev_free(a->dev);
	a->dev = NULL;

	dbglog(core, d->trk, "dsound", "%s buffer %ums, %uHz"
		, "opened", a->buffer_length_msec
		, fmt.sample_rate);

	fmed_timer_set(&ds->tmr, audio_out_onplay, a);
	if (0 != core->timer(&ds->tmr, a->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;
}

static int dsnd_write(void *ctx, fmed_filt *d)
{
	dsnd_out *ds = ctx;
	audio_out *a = &ds->out;
	int r;

	switch (a->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough
	case I_OPEN:
		if (0 != (r = dsnd_create(ds, d)))
			return r;
		a->state = I_DATA;
		break;

	case I_DATA:
		break;
	}

	r = audio_out_write(a, d);
	if (r == FMED_RERR) {
		core->timer(&ds->tmr, 0, 0);
		return FMED_RERR;
	}
	return r;
}


typedef struct dsnd_in {
	audio_in in;
	fftimerqueue_node tmr;
} dsnd_in;

static int dsnd_in_config(fmed_conf_ctx *ctx)
{
	dsnd_in_conf.idev = 0;
	dsnd_in_conf.buflen = 500;
	fmed_conf_addctx(ctx, &dsnd_in_conf, dsnd_in_conf_args);
	return 0;
}

static void* dsnd_in_open(fmed_filt *d)
{
	dsnd_in *ds = ffmem_new(dsnd_in);
	audio_in *a = &ds->in;
	a->core = core;
	a->audio = &ffdsound;
	a->track = track;
	a->trk = d->trk;

	int idx;
	if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "capture_device"))) {
		// use device specified by user
		a->dev_idx = idx;
	} else {
		a->dev_idx = dsnd_in_conf.idev;
	}

	a->buffer_length_msec = dsnd_in_conf.buflen;

	if (0 != audio_in_open(a, d))
		goto fail;

	fmed_timer_set(&ds->tmr, audio_oncapt, a);
	if (0 != core->timer(&ds->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return ds;

fail:
	dsnd_in_close(ds);
	return NULL;
}

static void dsnd_in_close(void *ctx)
{
	dsnd_in *ds = ctx;
	core->timer(&ds->tmr, 0, 0);
	audio_in_close(&ds->in);
}

static int dsnd_in_read(void *ctx, fmed_filt *d)
{
	dsnd_in *a = ctx;
	return audio_in_read(&a->in, d);
}
