/** Pulse input/output.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>

#undef errlog
#undef dbglog
#define errlog(trk, ...)  fmed_errlog(core, trk, "pulse", __VA_ARGS__)
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "pulse", __VA_ARGS__)


static const fmed_core *core;

typedef struct pulse_mod {
	fftimerqueue_node tmr;
	ffaudio_buf *out;
	ffpcm fmt;
	audio_out *usedby;
	const fmed_track *track;
	uint dev_idx;
	uint init_ok :1;
} pulse_mod;

static pulse_mod *mod;


enum { I_TRYOPEN, I_OPEN, I_DATA };

static struct pulse_out_conf_t {
	uint idev;
	uint buflen;
	uint nfy_rate;
} pulse_out_conf;

//FMEDIA MODULE
static const void* pulse_iface(const char *name);
static int pulse_conf(const char *name, fmed_conf_ctx *ctx);
static int pulse_sig(uint signo);
static void pulse_destroy(void);
static const fmed_mod fmed_pulse_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &pulse_iface,
	.sig = &pulse_sig,
	.destroy = &pulse_destroy,
	.conf = &pulse_conf,
};

static int mod_init(fmed_trk *trk);
static int pulse_create(audio_out *a, fmed_filt *d);
static int pulse_out_config(fmed_conf_ctx *ctx);

static const fmed_conf_arg pulse_out_conf_args[] = {
	{ "device_index",	FMC_INT32,  FMC_O(struct pulse_out_conf_t, idev) },
	{ "buffer_length",	FMC_INT32NZ,  FMC_O(struct pulse_out_conf_t, buflen) },
	{ "notify_rate",	FMC_INT32,  FMC_O(struct pulse_out_conf_t, nfy_rate) },
	{}
};

static const fmed_filter fmed_pulse_out;
static const fmed_filter fmed_pulse_in;

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_pulse_mod;
}


const fmed_adev fmed_pulse_adev;
static const void* pulse_iface(const char *name)
{
	if (ffsz_eq(name, "in")) {
		return &fmed_pulse_in;
	} else if (!ffsz_cmp(name, "out")) {
		return &fmed_pulse_out;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_pulse_adev;
	}
	return NULL;
}

static int pulse_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return pulse_out_config(ctx);
	return -1;
}

static int pulse_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_new(pulse_mod)))
			return -1;

		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

void pulse_buf_close()
{
	if (mod->out == NULL)
		return;
	dbglog(NULL, "free");
	ffpulse.free(mod->out);
	mod->out = NULL;
}

static void pulse_destroy(void)
{
	if (mod == NULL)
		return;

	pulse_buf_close();
	dbglog(NULL, "uninit");
	ffpulse.uninit();
	ffmem_free(mod);
	mod = NULL;
}

static int mod_init(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;

	dbglog(NULL, "init");
	ffaudio_init_conf conf = {};
	conf.app_name = "fmedia";
	if (0 != ffpulse.init(&conf)) {
		errlog(trk, "init: %s", conf.error);
		return -1;
	}

	mod->init_ok = 1;
	return 0;
}

static int pulse_adev_list(fmed_adev_ent **ents, uint flags)
{
	if (0 != mod_init(NULL))
		return -1;

	int r;
	if (0 > (r = audio_dev_list(core, &ffpulse, ents, flags, "pulse")))
		return -1;
	return r;
}

const fmed_adev fmed_pulse_adev = {
	pulse_adev_list, audio_dev_listfree, audio_dev_cmd,
};


static int pulse_out_config(fmed_conf_ctx *ctx)
{
	pulse_out_conf.idev = 0;
	pulse_out_conf.buflen = 500;
	pulse_out_conf.nfy_rate = 0;
	fmed_conf_addctx(ctx, &pulse_out_conf, pulse_out_conf_args);
	return 0;
}

static void* pulse_open(fmed_filt *d)
{
	audio_out *a;

	if (0 != mod_init(d->trk))
		return NULL;

	if (NULL == (a = ffmem_new(audio_out)))
		return NULL;
	a->core = core;
	a->audio = &ffpulse;
	a->track = mod->track;
	a->trk = d->trk;
	a->fx = d;
	d->adev = &fmed_pulse_adev;
	d->adev_ctx = a;
	return a;
}

void pulse_close_tmr(void *param)
{
	pulse_buf_close();
}

static void pulse_close(void *ctx)
{
	audio_out *a = ctx;

	if (mod->usedby == a) {
		if (0 != ffpulse.stop(mod->out))
			errlog(a->trk, "stop: %s", ffpulse.error(mod->out));
		if (a->fx->flags & FMED_FSTOP) {
			fmed_timer_set(&mod->tmr, pulse_close_tmr, NULL);
			core->timer(&mod->tmr, -ABUF_CLOSE_WAIT, 0);
		} else {
			core->timer(&mod->tmr, 0, 0);
		}
		mod->usedby = NULL;
	}

	ffpulse.dev_free(a->dev);
	ffmem_free(a);
}

static int pulse_create(audio_out *a, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(a->dev_idx = (int)d->track->getval(d->trk, "playdev_name")))
		a->dev_idx = pulse_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);
	a->buffer_length_msec = pulse_out_conf.buflen;
	a->try_open = (a->state == I_TRYOPEN);

	if (mod->out != NULL) {

		core->timer(&mod->tmr, 0, 0); // stop 'pulse_close_tmr' timer

		audio_out *cur = mod->usedby;
		if (cur != NULL) {
			mod->usedby = NULL;
			audio_out_onplay(cur);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& fmt.sample_rate == mod->fmt.sample_rate
			&& a->dev_idx == mod->dev_idx) {

			dbglog(a->trk, "reuse buffer: ffpulse.stop/clear");
			ffpulse.stop(mod->out);
			ffpulse.clear(mod->out);
			a->stream = mod->out;

			ffpulse.dev_free(a->dev);
			a->dev = NULL;

			reused = 1;
			goto fin;
		}

		pulse_buf_close();
	}

	while (0 != (r = audio_out_open(a, d, &fmt))) {
		if (r == FFAUDIO_EFORMAT) {
			a->state = I_OPEN;
			return FMED_RMORE;

		} else if (r == FFAUDIO_ECONNECTION) {
			if (!a->reconnect) {
				dbglog1(d->trk, "reconnecting...");
				a->reconnect = 1;

				ffpulse.uninit();
				mod->init_ok = 0;

				if (0 != mod_init(d->trk))
					return FMED_RERR;
				continue;
			}

			a->track->cmd(a->trk, FMED_TRACK_STOPPED);
		}
		return FMED_RERR;
	}

	ffpulse.dev_free(a->dev);
	a->dev = NULL;

	mod->out = a->stream;
	mod->fmt = fmt;
	mod->dev_idx = a->dev_idx;

fin:
	dbglog(d->trk, "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", a->buffer_length_msec
		, fmt.sample_rate);

	mod->usedby = a;

	fmed_timer_set(&mod->tmr, audio_out_onplay, a);
	if (0 != core->timer(&mod->tmr, a->buffer_length_msec / 3, 0))
		return FMED_RERR;

	return 0;
}

static int pulse_write(void *ctx, fmed_filt *d)
{
	audio_out *a = ctx;
	int r;

	switch (a->state) {
	case I_TRYOPEN:
		d->audio.convfmt.ileaved = 1;
		// fallthrough
	case I_OPEN:
		if (0 != (r = pulse_create(a, d)))
			return r;
		a->state = I_DATA;
		break;

	case I_DATA:
		break;
	}

	if (mod->usedby != a) {
		a->track->cmd(a->trk, FMED_TRACK_STOPPED);
		return FMED_RFIN;
	}

	r = audio_out_write(a, d);
	if (r == FMED_RERR) {
		pulse_buf_close();
		core->timer(&mod->tmr, 0, 0);
		mod->usedby = NULL;
		return FMED_RERR;
	}
	return r;
}

static const fmed_filter fmed_pulse_out = {
	pulse_open, pulse_write, pulse_close
};


typedef struct pulse_in {
	audio_in in;
	fftimerqueue_node tmr;
} pulse_in;

static void pulse_in_close(void *ctx);

static void* pulse_in_open(fmed_filt *d)
{
	if (0 != mod_init(d->trk))
		return NULL;

	pulse_in *pi = ffmem_new(pulse_in);
	audio_in *a = &pi->in;
	a->core = core;
	a->audio = &ffpulse;
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

	int r;
	int retry = 0;
	while (0 != (r = audio_in_open(a, d))) {
		if (!retry && r == FFAUDIO_ECONNECTION) {
			retry = 1;
			dbglog1(d->trk, "lost connection to audio server, reconnecting...");
			audio_in_close(&pi->in);

			a->audio->uninit();
			mod->init_ok = 0;

			if (0 != mod_init(d->trk)) {
				pulse_in_close(pi);
				return NULL;
			}
			continue;
		}

		goto fail;
	}

	fmed_timer_set(&pi->tmr, audio_oncapt, a);
	if (0 != core->timer(&pi->tmr, a->buffer_length_msec / 3, 0))
		goto fail;

	return pi;

fail:
	pulse_in_close(pi);
	return NULL;
}

static void pulse_in_close(void *ctx)
{
	pulse_in *pi = ctx;
	core->timer(&pi->tmr, 0, 0);
	audio_in_close(&pi->in);
	ffmem_free(pi);
}

static int pulse_in_read(void *ctx, fmed_filt *d)
{
	pulse_in *pi = ctx;
	return audio_in_read(&pi->in, d);
}

static const fmed_filter fmed_pulse_in = {
	pulse_in_open, pulse_in_read, pulse_in_close
};
