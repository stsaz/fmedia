/** OSS output.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/adev/oss.h>


static const fmed_core *core;

typedef struct oss_out oss_out;

typedef struct oss_mod {
	ffoss_buf out;
	ffpcm fmt;
	oss_out *usedby;
	const fmed_track *track;
	uint devidx;
	uint out_valid :1;
	uint init_ok :1;
} oss_mod;

static oss_mod *mod;

struct oss_out {
	uint state;
	size_t dataoff;

	ffoss_dev dev;
	uint devidx;

	void *trk;
	uint stop :1;
};

enum { I_TRYOPEN, I_OPEN, I_DATA };

static struct oss_out_conf_t {
	uint idev;
	uint buflen;
} oss_out_conf;

//FMEDIA MODULE
static const void* oss_iface(const char *name);
static int oss_conf(const char *name, ffpars_ctx *ctx);
static int oss_sig(uint signo);
static void oss_destroy(void);
static const fmed_mod fmed_oss_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &oss_iface,
	.sig = &oss_sig,
	.destroy = &oss_destroy,
	.conf = &oss_conf,
};

static int oss_init(fmed_trk *trk);
static int oss_create(oss_out *o, fmed_filt *d);

//OUTPUT
static void* oss_open(fmed_filt *d);
static int oss_write(void *ctx, fmed_filt *d);
static void oss_close(void *ctx);
static int oss_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_oss_out = {
	&oss_open, &oss_write, &oss_close
};

static const ffpars_arg oss_out_conf_args[] = {
	{ "device_index",	FFPARS_TINT,  FFPARS_DSTOFF(struct oss_out_conf_t, idev) },
	{ "buffer_length",	FFPARS_TINT,  FFPARS_DSTOFF(struct oss_out_conf_t, buflen) },
};

static void oss_onplay(void *udata);

//ADEV
static int oss_adev_list(fmed_adev_ent **ents, uint flags);
static void oss_adev_listfree(fmed_adev_ent *ents);
static const fmed_adev fmed_oss_adev = {
	.list = &oss_adev_list,
	.listfree = &oss_adev_listfree,
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
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_oss_adev;
	}
	return NULL;
}

static int oss_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return oss_out_config(ctx);
	return -1;
}

static int oss_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

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
	if (mod != NULL) {
		if (mod->out_valid)
			ffoss_close(&mod->out);
		ffmem_free(mod);
		mod = NULL;
	}

	ffoss_uninit();
}

static int oss_init(fmed_trk *trk)
{
	if (mod->init_ok)
		return 0;
	if (0 != ffoss_init()) {
		syserrlog(core, trk, NULL, "ffoss_init()");
		return -1;
	}
	mod->init_ok = 1;
	return 0;
}

static int oss_adev_list(fmed_adev_ent **ents, uint flags)
{
	ffarr o = {0};
	ffoss_dev d;
	uint f = 0;
	fmed_adev_ent *e;
	int r, rr = -1;

	if (mod == NULL && 0 != ffoss_init())
		return -1;

	ffoss_devinit(&d);

	if (flags == FMED_ADEV_PLAYBACK)
		f = FFOSS_DEV_PLAYBACK;
	else if (flags == FMED_ADEV_CAPTURE)
		f = FFOSS_DEV_CAPTURE;

	for (;;) {
		r = ffoss_devnext(&d, f);
		if (r == 1)
			break;
		else if (r < 0) {
			errlog(core, NULL, "oss", "ffoss_devnext(): (%d) %s", r, ffoss_errstr(r));
			goto end;
		}

		if (NULL == (e = ffarr_pushgrowT(&o, 4, fmed_adev_ent)))
			goto end;
		if (NULL == (e->name = ffsz_alcopyz(d.name)))
			goto end;
	}

	if (NULL == (e = ffarr_pushT(&o, fmed_adev_ent)))
		goto end;
	e->name = NULL;
	*ents = (void*)o.ptr;
	rr = o.len - 1;

end:
	ffoss_devdestroy(&d);
	if (rr < 0) {
		FFARR_WALKT(&o, e, fmed_adev_ent) {
			ffmem_safefree(e->name);
		}
		ffarr_free(&o);
	}
	return rr;
}

static void oss_adev_listfree(fmed_adev_ent *ents)
{
	fmed_adev_ent *e;
	for (e = ents;  e->name != NULL;  e++) {
		ffmem_free(e->name);
	}
	ffmem_free(ents);
}


static int oss_out_config(ffpars_ctx *ctx)
{
	oss_out_conf.idev = 0;
	oss_out_conf.buflen = 500;
	ffpars_setargs(ctx, &oss_out_conf, oss_out_conf_args, FFCNT(oss_out_conf_args));
	return 0;
}

static void* oss_open(fmed_filt *d)
{
	oss_out *o;

	if (0 != oss_init(d->trk))
		return NULL;

	if (NULL == (o = ffmem_new(oss_out)))
		return NULL;
	o->trk = d->trk;
	return o;
}

static void oss_close(void *ctx)
{
	oss_out *o = ctx;
	int r;

	if (mod->usedby == o) {
		void *trk = o->trk;

		if (FMED_NULL != mod->track->getval(trk, "stopped")) {
			ffoss_close(&mod->out);
			ffmem_tzero(&mod->out);
			mod->out_valid = 0;

		} else {
			if (0 != (r = ffoss_stop(&mod->out)))
				errlog(core, trk,  "oss", "ffoss_stop(): (%d) %s", r, ffoss_errstr(r));
			ffoss_clear(&mod->out);
		}

		mod->usedby = NULL;
	}

	ffoss_devdestroy(&o->dev);
	ffmem_free(o);
}

static int oss_devbyidx(ffoss_dev *d, uint idev, uint flags)
{
	ffoss_devinit(d);
	for (;  idev != 0;  idev--) {
		int r = ffoss_devnext(d, flags);
		if (r != 0) {
			ffoss_devdestroy(d);
			return r;
		}
	}
	return 0;
}

static int oss_create(oss_out *o, fmed_filt *d)
{
	ffpcm fmt, in_fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(o->devidx = (int)d->track->getval(d->trk, "playdev_name")))
		o->devidx = oss_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);

	if (mod->out_valid) {

		if (mod->usedby != NULL) {
			oss_out *o = mod->usedby;
			mod->usedby = NULL;
			o->stop = 1;
			oss_onplay(o);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& fmt.sample_rate == mod->fmt.sample_rate
			&& mod->devidx == o->devidx) {

			ffoss_stop(&mod->out);
			ffoss_clear(&mod->out);
			reused = 1;
			goto fin;
		}

		ffoss_close(&mod->out);
		ffmem_tzero(&mod->out);
		mod->out_valid = 0;
	}

	if (0 != oss_devbyidx(&o->dev, o->devidx, FFOSS_DEV_PLAYBACK)) {
		errlog(core, d->trk, "oss", "no audio device by index #%u", o->devidx);
		goto done;
	}

	in_fmt = fmt;
	r = ffoss_open(&mod->out, o->dev.id, &fmt, oss_out_conf.buflen, FFOSS_DEV_PLAYBACK);

	if (r == -FFOSS_EFMT && o->state == I_TRYOPEN) {

		if (!!ffmemcmp(&fmt, &in_fmt, sizeof(ffpcmex))) {

			if (fmt.format != in_fmt.format)
				d->audio.convfmt.format = fmt.format;

			if (fmt.sample_rate != in_fmt.sample_rate)
				d->audio.convfmt.sample_rate = fmt.sample_rate;

			if (fmt.channels != in_fmt.channels)
				d->audio.convfmt.channels = fmt.channels;

			o->state = I_OPEN;
			return FMED_RMORE;
		}
	}

	if (r != 0) {
		errlog(core, d->trk, "oss", "ffoss_open(): (%d) %s", r, ffoss_errstr(r));
		goto done;
	}

	ffoss_devdestroy(&o->dev);
	mod->out_valid = 1;
	mod->fmt = fmt;
	mod->devidx = o->devidx;

fin:
	mod->usedby = o;
	dbglog(core, d->trk, "oss", "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", ffpcm_bytes2time(&fmt, ffoss_bufsize(&mod->out))
		, fmt.sample_rate);
	return 0;

done:
	return FMED_RERR;
}

static void oss_onplay(void *udata)
{
	oss_out *o = udata;
	mod->track->cmd(o->trk, FMED_TRACK_WAKE);
}

static int oss_write(void *ctx, fmed_filt *d)
{
	oss_out *o = ctx;
	int r;

	switch (o->state) {
	case I_TRYOPEN:
	case I_OPEN:
		d->audio.convfmt.ileaved = 1;
		if (0 != (r = oss_create(o, d)))
			return r;
		o->state = I_DATA;
		return FMED_RMORE;

	case I_DATA:
		break;
	}

	if (o->stop || (d->flags & FMED_FSTOP)) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if (d->snd_output_clear) {
		d->snd_output_clear = 0;
		ffoss_stop(&mod->out);
		ffoss_clear(&mod->out);
		o->dataoff = 0;
		return FMED_RMORE;
	}

	while (d->datalen != 0) {

		r = ffoss_write(&mod->out, d->data, d->datalen, o->dataoff);
		if (r < 0) {
			errlog(core, d->trk, "oss", "ffoss_write(): (%d) %s", r, ffoss_errstr(r));
			goto err;

		} else if (r == 0) {
			return FMED_RASYNC;
		}

		o->dataoff += r;
		d->datalen -= r;
		dbglog(core, d->trk, "oss", "written %u bytes (%u%% filled)"
			, r, ffoss_filled(&mod->out) * 100 / ffoss_bufsize(&mod->out));
	}

	o->dataoff = 0;

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffoss_drain(&mod->out);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "oss", "ffoss_drain(): (%d) %s", r, ffoss_errstr(r));
			goto err;
		}

		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;

err:
	ffoss_close(&mod->out);
	ffmem_tzero(&mod->out);
	mod->out_valid = 0;
	mod->usedby = NULL;
	return FMED_RERR;
}
