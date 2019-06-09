/** Pulse output.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/adev/pulse.h>


static const fmed_core *core;

typedef struct pulse_out pulse_out;

typedef struct pulse_mod {
	ffpulse_buf out;
	ffpcm fmt;
	pulse_out *usedby;
	const fmed_track *track;
	uint devidx;
	uint out_valid :1;
	uint init_ok :1;
} pulse_mod;

static pulse_mod *mod;

struct pulse_out {
	uint state;
	size_t dataoff;

	ffpulse_dev dev;
	uint devidx;

	void *trk;
	uint stop :1;
};

enum { I_OPEN, I_DATA };

static struct pulse_out_conf_t {
	uint idev;
	uint buflen;
	uint nfy_rate;
} pulse_out_conf;

//FMEDIA MODULE
static const void* pulse_iface(const char *name);
static int pulse_conf(const char *name, ffpars_ctx *ctx);
static int pulse_sig(uint signo);
static void pulse_destroy(void);
static const fmed_mod fmed_pulse_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &pulse_iface,
	.sig = &pulse_sig,
	.destroy = &pulse_destroy,
	.conf = &pulse_conf,
};

static int pulse_init(fmed_trk *trk);
static int pulse_create(pulse_out *a, fmed_filt *d);

//OUTPUT
static void* pulse_open(fmed_filt *d);
static int pulse_write(void *ctx, fmed_filt *d);
static void pulse_close(void *ctx);
static int pulse_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_pulse_out = {
	&pulse_open, &pulse_write, &pulse_close
};

static const ffpars_arg pulse_out_conf_args[] = {
	{ "device_index",	FFPARS_TINT,  FFPARS_DSTOFF(struct pulse_out_conf_t, idev) },
	{ "buffer_length",	FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct pulse_out_conf_t, buflen) },
	{ "notify_rate",	FFPARS_TINT,  FFPARS_DSTOFF(struct pulse_out_conf_t, nfy_rate) },
};

static void pulse_onplay(void *udata);

//ADEV
static int pulse_adev_list(fmed_adev_ent **ents, uint flags);
static void pulse_adev_listfree(fmed_adev_ent *ents);
static const fmed_adev fmed_pulse_adev = {
	.list = &pulse_adev_list,
	.listfree = &pulse_adev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_pulse_mod;
}


static const void* pulse_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_pulse_out;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_pulse_adev;
	}
	return NULL;
}

static int pulse_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return pulse_out_config(ctx);
	return -1;
}

static int pulse_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		if (NULL == (mod = ffmem_new(pulse_mod)))
			return -1;

		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void pulse_destroy(void)
{
	if (mod != NULL) {
		if (mod->out_valid)
			ffpulse_close(&mod->out);
		ffmem_free(mod);
		mod = NULL;
	}

	ffpulse_uninit();
}

static int pulse_init(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;
	int r;
	if (0 != (r = ffpulse_init(core->kq))) {
		errlog(core, trk, NULL, "ffpulse_init(): %s", ffpulse_errstr(r));
		return -1;
	}
	mod->init_ok = 1;
	return 0;
}

static int pulse_adev_list(fmed_adev_ent **ents, uint flags)
{
	ffarr a = {0};
	ffpulse_dev d;
	uint f = 0;
	fmed_adev_ent *e;
	int r, rr = -1;

	if ((mod == NULL || !mod->init_ok)
		&& 0 != (r = ffpulse_init(FF_BADFD))) {
		errlog(core, NULL, NULL, "ffpulse_init(): %s", ffpulse_errstr(r));
		return -1;
	}

	ffpulse_devinit(&d);

	if (flags == FMED_ADEV_PLAYBACK)
		f = FFPULSE_DEV_PLAYBACK;
	else if (flags == FMED_ADEV_CAPTURE)
		f = FFPULSE_DEV_CAPTURE;

	for (;;) {
		r = ffpulse_devnext(&d, f);
		if (r == 1)
			break;
		else if (r < 0) {
			errlog(core, NULL, "pulse", "ffpulse_devnext(): (%d) %s", r, ffpulse_errstr(r));
			goto end;
		}

		if (NULL == (e = ffarr_pushgrowT(&a, 4, fmed_adev_ent)))
			goto end;
		ffmem_tzero(e);
		if (NULL == (e->name = ffsz_alcopyz(d.name)))
			goto end;
	}

	if (NULL == (e = ffarr_pushT(&a, fmed_adev_ent)))
		goto end;
	e->name = NULL;
	*ents = (void*)a.ptr;
	rr = a.len - 1;

end:
	ffpulse_devdestroy(&d);
	if (rr < 0) {
		FFARR_WALKT(&a, e, fmed_adev_ent) {
			ffmem_safefree(e->name);
		}
		ffarr_free(&a);
	}
	return rr;
}

static void pulse_adev_listfree(fmed_adev_ent *ents)
{
	fmed_adev_ent *e;
	for (e = ents;  e->name != NULL;  e++) {
		ffmem_free(e->name);
	}
	ffmem_free(ents);
}


static int pulse_out_config(ffpars_ctx *ctx)
{
	pulse_out_conf.idev = 0;
	pulse_out_conf.buflen = 500;
	pulse_out_conf.nfy_rate = 0;
	ffpars_setargs(ctx, &pulse_out_conf, pulse_out_conf_args, FFCNT(pulse_out_conf_args));
	return 0;
}

static void* pulse_open(fmed_filt *d)
{
	pulse_out *a;

	if (0 != pulse_init(d->trk))
		return NULL;

	if (NULL == (a = ffmem_new(pulse_out)))
		return NULL;
	a->trk = d->trk;
	return a;
}

static void pulse_close(void *ctx)
{
	pulse_out *a = ctx;
	int r;

	if (mod->usedby == a) {
		void *trk = a->trk;

		if (FMED_NULL != mod->track->getval(trk, "stopped")) {
			ffpulse_close(&mod->out);
			ffmem_tzero(&mod->out);
			mod->out_valid = 0;

		} else {
			if (0 != (r = ffpulse_stop(&mod->out)))
				errlog(core, trk,  "pulse", "ffpulse_stop(): (%d) %s", r, ffpulse_errstr(r));
			ffpulse_clear(&mod->out);
			ffpulse_async(&mod->out, 0);
		}

		mod->usedby = NULL;
	}

	ffpulse_devdestroy(&a->dev);
	ffmem_free(a);
}

static int pulse_devbyidx(ffpulse_dev *d, uint idev, uint flags)
{
	ffpulse_devinit(d);
	for (;  idev != 0;  idev--) {
		int r = ffpulse_devnext(d, flags);
		if (r != 0) {
			ffpulse_devdestroy(d);
			return r;
		}
	}
	return 0;
}

static int pulse_create(pulse_out *a, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(a->devidx = (int)d->track->getval(d->trk, "playdev_name")))
		a->devidx = pulse_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);

	if (mod->out_valid) {

		if (mod->usedby != NULL) {
			pulse_out *a = mod->usedby;
			mod->usedby = NULL;
			a->stop = 1;
			pulse_onplay(a);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& fmt.sample_rate == mod->fmt.sample_rate
			&& mod->devidx == a->devidx) {

			ffpulse_stop(&mod->out);
			ffpulse_clear(&mod->out);
			ffpulse_async(&mod->out, 0);
			reused = 1;
			goto fin;
		}

		ffpulse_close(&mod->out);
		ffmem_tzero(&mod->out);
		mod->out_valid = 0;
	}

	if (0 != pulse_devbyidx(&a->dev, a->devidx, FFPULSE_DEV_PLAYBACK)) {
		errlog(core, d->trk, "pulse", "no audio device by index #%u", a->devidx);
		goto done;
	}

	mod->out.handler = &pulse_onplay;
	mod->out.autostart = 1;
	if (pulse_out_conf.nfy_rate != 0)
		mod->out.nfy_interval = ffpcm_bytes2time(&fmt, pulse_out_conf.buflen) / pulse_out_conf.nfy_rate;
	r = ffpulse_open(&mod->out, a->dev.id, &fmt, pulse_out_conf.buflen);

	if (r != 0) {
		errlog(core, d->trk, "pulse", "ffpulse_open(): (%d) %s", r, ffpulse_errstr(r));
		goto done;
	}

	ffpulse_devdestroy(&a->dev);
	mod->out_valid = 1;
	mod->fmt = fmt;
	mod->devidx = a->devidx;

fin:
	mod->out.udata = a;
	mod->usedby = a;
	dbglog(core, d->trk, "pulse", "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", ffpcm_bytes2time(&fmt, ffpulse_bufsize(&mod->out))
		, fmt.sample_rate);
	return 0;

done:
	return FMED_RERR;
}

static void pulse_onplay(void *udata)
{
	pulse_out *a = udata;
	mod->track->cmd(a->trk, FMED_TRACK_WAKE);
}

static int pulse_write(void *ctx, fmed_filt *d)
{
	pulse_out *a = ctx;
	int r;

	switch (a->state) {
	case I_OPEN:
		d->audio.convfmt.ileaved = 1;
		if (0 != (r = pulse_create(a, d)))
			return r;
		a->state = I_DATA;
		return FMED_RMORE;

	case I_DATA:
		break;
	}

	if (a->stop || (d->flags & FMED_FSTOP)) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if (d->snd_output_clear) {
		d->snd_output_clear = 0;
		ffpulse_stop(&mod->out);
		ffpulse_clear(&mod->out);
		ffpulse_async(&mod->out, 0);
		a->dataoff = 0;
		return FMED_RMORE;
	}

	if (d->snd_output_pause) {
		d->snd_output_pause = 0;
		d->track->cmd(d->trk, FMED_TRACK_PAUSE);
		ffpulse_stop(&mod->out);
		ffpulse_async(&mod->out, 0);
		return FMED_RMORE;
	}

	while (d->datalen != 0) {

		r = ffpulse_write(&mod->out, d->data, d->datalen, a->dataoff);
		if (r < 0) {
			errlog(core, d->trk, "pulse", "ffpulse_write(): (%d) %s", r, ffpulse_errstr(r));
			goto err;

		} else if (r == 0) {
			ffpulse_async(&mod->out, 1);
			return FMED_RASYNC;
		}

		a->dataoff += r;
		d->datalen -= r;
		dbglog(core, d->trk, "pulse", "written %u bytes (%u%% filled)"
			, r, ffpulse_filled(&mod->out) * 100 / ffpulse_bufsize(&mod->out));
	}

	a->dataoff = 0;

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffpulse_drain(&mod->out);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "pulse", "ffpulse_drain(): (%d) %s", r, ffpulse_errstr(r));
			goto err;
		}

		ffpulse_async(&mod->out, 1);
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;

err:
	ffpulse_close(&mod->out);
	ffmem_tzero(&mod->out);
	mod->out_valid = 0;
	mod->usedby = NULL;
	return FMED_RERR;
}
