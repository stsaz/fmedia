/** WASAPI input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>

#undef dbglog1
#define dbglog1(t, fmt, ...)  fmed_dbglog(core, t, "wasapi", fmt, ##__VA_ARGS__)

static const fmed_core *core;

typedef struct wasapi_mod {
	ffaudio_buf *out;
	fftimerqueue_node tmr;
	ffpcm fmt;
	ffvec fmts; // struct fmt_pair[]
	audio_out *usedby;
	const fmed_track *track;
	uint dev_idx;
	uint init_ok :1;
	uint excl :1;
} wasapi_mod;

static wasapi_mod *mod;

enum { I_TRYOPEN, I_OPEN, I_DATA };

enum EXCL {
	EXCL_DISABLED
	, EXCL_ALLOWED
	, EXCL_ALWAYS
};

static struct wasapi_out_conf_t {
	uint idev;
	uint buflen;
	byte exclusive;
} wasapi_out_conf;

static struct wasapi_in_conf_t {
	uint idev;
	uint buflen;
	byte exclusive;
	byte latency_autocorrect;
} wasapi_in_conf;

//FMEDIA MODULE
static const void* wasapi_iface(const char *name);
static int wasapi_conf(const char *name, fmed_conf_ctx *ctx);
static int wasapi_sig(uint signo);
static void wasapi_destroy(void);
static const fmed_mod fmed_wasapi_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&wasapi_iface, &wasapi_sig, &wasapi_destroy, &wasapi_conf
};

static int wasapi_init(fmed_trk *trk);

//OUTPUT
static void* wasapi_open(fmed_filt *d);
static int wasapi_write(void *ctx, fmed_filt *d);
static void wasapi_close(void *ctx);
static int wasapi_out_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_wasapi_out = {
	&wasapi_open, &wasapi_write, &wasapi_close
};

static const fmed_conf_arg wasapi_out_conf_args[] = {
	{ "device_index",  FMC_INT32,  FMC_O(struct wasapi_out_conf_t, idev) }
	, { "buffer_length",  FMC_INT32NZ,  FMC_O(struct wasapi_out_conf_t, buflen) }
	, { "exclusive_mode",  FMC_INT8,  FMC_O(struct wasapi_out_conf_t, exclusive) },
	{}
};

//INPUT
static void* wasapi_in_open(fmed_filt *d);
static int wasapi_in_read(void *ctx, fmed_filt *d);
static void wasapi_in_close(void *ctx);
static int wasapi_in_config(fmed_conf_ctx *ctx);
static const fmed_filter fmed_wasapi_in = {
	&wasapi_in_open, &wasapi_in_read, &wasapi_in_close
};

static const fmed_conf_arg wasapi_in_conf_args[] = {
	{ "device_index",  FMC_INT32,  FMC_O(struct wasapi_in_conf_t, idev) }
	, { "buffer_length",  FMC_INT32NZ,  FMC_O(struct wasapi_in_conf_t, buflen) }
	, { "latency_autocorrect",  FMC_BOOL8,  FMC_O(struct wasapi_in_conf_t, latency_autocorrect) }
	, { "exclusive_mode",  FMC_INT8,  FMC_O(struct wasapi_in_conf_t, exclusive) },
	{}
};

//ADEV
static int wasapi_adev_list(fmed_adev_ent **ents, uint flags);
static const fmed_adev fmed_wasapi_adev = {
	.list = &wasapi_adev_list,
	.listfree = audio_dev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_wasapi_mod;
}


static const void* wasapi_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_wasapi_out;
	} else if (!ffsz_cmp(name, "in")) {
		return &fmed_wasapi_in;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_wasapi_adev;
	}
	return NULL;
}

static int wasapi_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return wasapi_out_config(ctx);
	else if (!ffsz_cmp(name, "in"))
		return wasapi_in_config(ctx);
	return -1;
}

static int wasapi_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_tcalloc1(wasapi_mod)))
			return -1;
		mod->track = core->getmod("#core.track");
		core->props->playback_dev_index = wasapi_out_conf.idev;
		return 0;
	}
	return 0;
}

static void wasapi_buf_close()
{
	if (mod->out == NULL)
		return;
	dbglog1(NULL, "free buffer");
	ffwasapi.free(mod->out);
	mod->out = NULL;
}

static void wasapi_destroy(void)
{
	if (mod == NULL)
		return;

	wasapi_buf_close();
	ffvec_free(&mod->fmts);
	ffwasapi.uninit();
	ffmem_free(mod);
	mod = NULL;
}

static int wasapi_init(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;

	ffaudio_init_conf conf = {};
	if (0 != ffwasapi.init(&conf)) {
		errlog(core, trk, "wasapi", "init: %s", conf.error);
		return -1;
	}

	mod->init_ok = 1;
	return 0;
}


static int wasapi_adev_list(fmed_adev_ent **ents, uint flags)
{
	if (0 != wasapi_init(NULL))
		return -1;

	int r;
	if (0 > (r = audio_dev_list(core, &ffwasapi, ents, flags, "wasapi")))
		return -1;
	return r;
}

static int wasapi_out_config(fmed_conf_ctx *ctx)
{
	wasapi_out_conf.idev = 0;
	wasapi_out_conf.exclusive = EXCL_DISABLED;
	wasapi_out_conf.buflen = 500;
	fmed_conf_addctx(ctx, &wasapi_out_conf, wasapi_out_conf_args);
	return 0;
}

static void* wasapi_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, "wasapi", "unsupported input data type: %s", d->datatype);
		return NULL;
	}

	if (0 != wasapi_init(d->trk))
		return NULL;

	audio_out *w;
	w = ffmem_new(audio_out);
	if (w == NULL)
		return NULL;
	w->core = core;
	w->audio = &ffwasapi;
	w->track = mod->track;
	w->trk = d->trk;
	w->fx = d;
	return w;
}

/**
Return FMED_RMORE (if state==I_TRYOPEN): requesting audio conversion */
static int wasapi_create(audio_out *w, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(w->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		w->dev_idx = wasapi_out_conf.idev;
	if (w->dev_idx == 0)
		w->handle_dev_offline = 1;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	w->buffer_length_msec = wasapi_out_conf.buflen;
	w->try_open = (w->state == I_TRYOPEN);

	int excl = 0;
	int64 lowlat;
	if (wasapi_out_conf.exclusive == EXCL_ALLOWED && FMED_NULL != (lowlat = d->track->getval(d->trk, "low_latency")))
		excl = !!lowlat;
	else if (wasapi_out_conf.exclusive == EXCL_ALWAYS)
		excl = 1;

	if (mod->out != NULL) {

		core->timer(&mod->tmr, 0, 0); // stop 'wasapi_close_tmr' timer

		audio_out *cur = mod->usedby;
		if (cur != NULL) {
			mod->usedby = NULL;
			audio_out_onplay(cur);
		}

		// Note: we don't support cases when devices are switched
		if (mod->dev_idx == w->dev_idx && mod->excl == excl) {
			if (ffpcm_eq(&fmt, &mod->fmt)) {
				dbglog1(NULL, "stop/clear");
				ffwasapi.stop(mod->out);
				ffwasapi.clear(mod->out);
				w->stream = mod->out;

				ffwasapi.dev_free(w->dev);
				w->dev = NULL;

				reused = 1;
				goto fin;
			}

			const ffpcm *good_fmt;
			if (w->try_open && NULL != (good_fmt = fmt_conv_find(&mod->fmts, &fmt))
				&& ffpcm_eq(good_fmt, &mod->fmt)) {
				// Don't try to reopen the buffer, because it's likely to fail again.
				// Instead, just use the format ffaudio set for us previously.
				ffpcm_fmtcopy(&d->audio.convfmt, good_fmt);
				w->state = I_OPEN;
				return FMED_RMORE;
			}
		}

		wasapi_buf_close();
	}

	if (excl)
		w->aflags = FFAUDIO_O_EXCLUSIVE;
	w->aflags |= FFAUDIO_O_UNSYNC_NOTIFY;
	r = audio_out_open(w, d, &fmt);
	if (r == FFAUDIO_EFORMAT) {
		fmt_conv_add(&mod->fmts, &fmt, &d->audio.convfmt);
		w->state = I_OPEN;
		return FMED_RMORE;
	} else if (r != 0)
		return FMED_RERR;

	ffwasapi.dev_free(w->dev);
	w->dev = NULL;

	mod->out = w->stream;
	mod->excl = excl;
	mod->fmt = fmt;
	mod->dev_idx = w->dev_idx;

fin:
	mod->usedby = w;
	dbglog1(d->trk, "%s buffer %ums, %s/%uHz/%u, excl:%u"
		, reused ? "reused" : "opened", w->buffer_length_msec
		, ffpcm_format_str(mod->fmt.format), mod->fmt.sample_rate, mod->fmt.channels
		, excl);

	fmed_timer_set(&mod->tmr, audio_out_onplay, w);
	if (0 != core->timer(&mod->tmr, w->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;
}

void wasapi_close_tmr(void *param)
{
	wasapi_buf_close();
}

static void wasapi_close(void *ctx)
{
	audio_out *w = ctx;
	if (mod->usedby == w) {
		if (0 != ffwasapi.stop(mod->out))
			errlog(core, w->trk, "wasapi", "stop: %s", ffwasapi.error(mod->out));
		if (w->fx->flags & FMED_FSTOP) {
			fmed_timer_set(&mod->tmr, wasapi_close_tmr, NULL);
			core->timer(&mod->tmr, -ABUF_CLOSE_WAIT, 0);
		} else {
			core->timer(&mod->tmr, 0, 0);
		}

		mod->usedby = NULL;
	}

	ffwasapi.dev_free(w->dev);
	ffmem_free(w);
}

static int wasapi_write(void *ctx, fmed_filt *d)
{
	audio_out *w = ctx;
	int r;

	switch (w->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough

	case I_OPEN:
		if (0 != (r = wasapi_create(w, d)))
			return r;
		w->state = I_DATA;
		break;

	case I_DATA:
		break;
	}

	if (mod->usedby != w) {
		w->track->cmd(w->trk, FMED_TRACK_STOPPED);
		return FMED_RFIN;
	}

	r = audio_out_write(w, d);
	if (r == FMED_RERR) {
		wasapi_buf_close();
		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
		if (w->err_code == FFAUDIO_EDEV_OFFLINE && w->dev_idx == 0) {
			/*
			This code works only because shared mode WASAPI has the same audio format for all devices
			 so we won't request a new format conversion.
			For exclusive mode we need to handle new format conversion properly which isn't that easy to do.
			*/
			w->state = I_OPEN;
			return wasapi_write(w, d);
		}
		return FMED_RERR;
	}
	return r;
}


typedef struct was_in {
	audio_in in;
	fftimerqueue_node tmr;
	uint latcorr;
} was_in;

static int wasapi_in_config(fmed_conf_ctx *ctx)
{
	wasapi_in_conf.idev = 0;
	wasapi_in_conf.exclusive = EXCL_DISABLED;
	wasapi_in_conf.latency_autocorrect = 0;
	wasapi_in_conf.buflen = 100;
	fmed_conf_addctx(ctx, &wasapi_in_conf, wasapi_in_conf_args);
	return 0;
}

static void* wasapi_in_open(fmed_filt *d)
{
	if (0 != wasapi_init(d->trk))
		return NULL;

	was_in *wi = ffmem_new(was_in);
	audio_in *a = &wi->in;
	a->core = core;
	a->audio = &ffwasapi;
	a->track = mod->track;
	a->trk = d->trk;

	int idx;
	if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "loopback_device"))) {
		// use loopback device specified by user
		a->loopback = 1;
		a->dev_idx = idx;
	} else if (FMED_NULL != (idx = (int)d->track->getval(d->trk, "capture_device"))) {
		// use device specified by user
		a->dev_idx = idx;
	} else {
		a->dev_idx = wasapi_in_conf.idev;
	}

	a->buffer_length_msec = wasapi_in_conf.buflen;

	int excl = 0;
	int lowlat;
	if (wasapi_in_conf.exclusive == EXCL_ALLOWED
		&& FMED_NULL != (lowlat = (int)fmed_getval("low_latency")))
		excl = lowlat;
	else if (wasapi_in_conf.exclusive == EXCL_ALWAYS)
		excl = 1;
	if (excl)
		a->aflags = FFAUDIO_O_EXCLUSIVE;

	a->aflags |= FFAUDIO_O_UNSYNC_NOTIFY;
	if (0 != audio_in_open(a, d))
		goto fail;

	fmed_timer_set(&wi->tmr, audio_oncapt, a);
	if (0 != core->timer(&wi->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return wi;

fail:
	wasapi_in_close(wi);
	return NULL;
}

static void wasapi_in_close(void *ctx)
{
	was_in *wi = ctx;
	audio_in *a = &wi->in;
	core->timer(&wi->tmr, 0, 0);
	audio_in_close(a);
	ffmem_free(wi);
}

static int wasapi_in_read(void *ctx, fmed_filt *d)
{
	was_in *wi = ctx;
	return audio_in_read(&wi->in, d);
}
