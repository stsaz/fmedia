/** ALSA input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/adev/alsa.h>


static const fmed_core *core;

typedef struct alsa_out alsa_out;

typedef struct alsa_mod {
	ffalsa_buf out;
	ffpcm fmt;
	alsa_out *usedby;
	const fmed_track *track;
	uint devidx;
	uint out_valid :1;
} alsa_mod;

static alsa_mod *mod;

struct alsa_out {
	uint state;
	size_t dataoff;

	ffalsa_dev dev;
	uint devidx;

	struct {
		fftask_handler handler;
		void *param;
	} task;
	uint stop :1;
};

enum { I_OPEN, I_DATA };

static struct alsa_out_conf_t {
	uint idev;
	uint buflen;
	uint nfy_rate;
} alsa_out_conf;

//FMEDIA MODULE
static const void* alsa_iface(const char *name);
static int alsa_sig(uint signo);
static void alsa_destroy(void);
static const fmed_mod fmed_alsa_mod = {
	&alsa_iface, &alsa_sig, &alsa_destroy
};

static int alsa_listdev(void);
static int alsa_create(alsa_out *a, fmed_filt *d);

//OUTPUT
static void* alsa_open(fmed_filt *d);
static int alsa_write(void *ctx, fmed_filt *d);
static void alsa_close(void *ctx);
static int alsa_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_alsa_out = {
	&alsa_open, &alsa_write, &alsa_close, &alsa_out_config
};

static const ffpars_arg alsa_out_conf_args[] = {
	{ "device_index",	FFPARS_TINT,  FFPARS_DSTOFF(struct alsa_out_conf_t, idev) },
	{ "buffer_length",	FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct alsa_out_conf_t, buflen) },
	{ "notify_rate",	FFPARS_TINT,  FFPARS_DSTOFF(struct alsa_out_conf_t, nfy_rate) },
};

static void alsa_onplay(void *udata);

//INPUT
static void* alsa_in_open(fmed_filt *d);
static int alsa_in_read(void *ctx, fmed_filt *d);
static void alsa_in_close(void *ctx);
static int alsa_in_config(ffpars_ctx *ctx);
static const fmed_filter fmed_alsa_in = {
	&alsa_in_open, &alsa_in_read, &alsa_in_close, &alsa_in_config
};

typedef struct alsa_mod_in {
	ffalsa_buf snd;
} alsa_mod_in;

static alsa_mod_in *ain;

typedef struct alsa_in {
	void *bufs[8];
	struct {
		fftask_handler handler;
		void *param;
	} cb;
	uint64 total_samps;
} alsa_in;

static struct alsa_in_conf_t {
	uint idev;
	uint buflen;
} alsa_in_conf;

static const ffpars_arg alsa_in_conf_args[] = {
	{ "device_index",	FFPARS_TINT,  FFPARS_DSTOFF(struct alsa_in_conf_t, idev) },
	{ "buffer_length",	FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct alsa_in_conf_t, buflen) },
};

static void alsa_in_oncapt(void *udata);


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
	}
	return NULL;
}

static int alsa_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		if (NULL == (mod = ffmem_tcalloc1(alsa_mod)))
			return -1;

		if (NULL == (ain = ffmem_tcalloc1(alsa_mod_in))) {
			ffmem_free0(mod);
			return -1;
		}

		if (0 != ffalsa_init(core->kq)) {
			ffmem_free0(mod);
			ffmem_free0(ain);
			return -1;
		}

		mod->track = core->getmod("#core.track");
		return 0;

	case FMED_LISTDEV:
		return alsa_listdev();
	}
	return 0;
}

static void alsa_destroy(void)
{
	if (mod != NULL) {
		if (mod->out_valid)
			ffalsa_close(&mod->out);
		ffmem_free(mod);
		mod = NULL;
	}

	ffmem_safefree0(ain);

	ffalsa_uninit(core->kq);
}

static int alsa_listdev(void)
{
	ffalsa_dev d;
	int i, r;
	ffstr3 buf = {0};

	ffalsa_devinit(&d);
	ffstr_catfmt(&buf, "Playback:\n");
	for (i = 1;  0 == (r = ffalsa_devnext(&d, FFALSA_DEV_PLAYBACK));  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d.name);
	}
	ffalsa_devdestroy(&d);
	if (r < 0)
		goto fail;

	ffalsa_devinit(&d);
	ffstr_catfmt(&buf, "\nCapture:\n");
	for (i = 1;  0 == (r = ffalsa_devnext(&d, FFALSA_DEV_CAPTURE));  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d.name);
	}
	ffalsa_devdestroy(&d);
	if (r < 0)
		goto fail;

	fffile_write(ffstdout, buf.ptr, buf.len);
	ffarr_free(&buf);
	return 1;

fail:
	errlog(core, NULL, "alsa", "ffalsa_devnext(): (%d) %s", r, ffalsa_errstr(r));
	ffarr_free(&buf);
	return -1;
}


static int alsa_out_config(ffpars_ctx *ctx)
{
	alsa_out_conf.idev = 0;
	alsa_out_conf.buflen = 500;
	alsa_out_conf.nfy_rate = 0;
	ffpars_setargs(ctx, &alsa_out_conf, alsa_out_conf_args, FFCNT(alsa_out_conf_args));
	return 0;
}

static void* alsa_open(fmed_filt *d)
{
	alsa_out *a;
	if (NULL == (a = ffmem_tcalloc1(alsa_out)))
		return NULL;
	a->task.handler = d->handler;
	a->task.param = d->trk;
	return a;
}

static void alsa_close(void *ctx)
{
	alsa_out *a = ctx;
	int r;

	if (mod->usedby == a) {
		void *trk = a->task.param;

		if (FMED_NULL != mod->track->getval(trk, "stopped")) {
			ffalsa_close(&mod->out);
			ffmem_tzero(&mod->out);
			mod->out_valid = 0;

		} else {
			if (0 != (r = ffalsa_stop(&mod->out)))
				errlog(core, trk,  "alsa", "ffalsa_stop(): (%d) %s", r, ffalsa_errstr(r));
			ffalsa_clear(&mod->out);
			ffalsa_async(&mod->out, 0);
		}

		mod->usedby = NULL;
	}

	ffalsa_devdestroy(&a->dev);
	ffmem_free(a);
}

static int alsa_devbyidx(ffalsa_dev *d, uint idev, uint flags)
{
	ffalsa_devinit(d);
	for (;  idev != 0;  idev--) {
		int r = ffalsa_devnext(d, flags);
		if (r != 0) {
			ffalsa_devdestroy(d);
			return r;
		}
	}
	return 0;
}

static int alsa_create(alsa_out *a, fmed_filt *d)
{
	ffpcm fmt;
	int r, reused = 0;

	if (FMED_NULL == (int)(a->devidx = (int)d->track->getval(d->trk, "playdev_name")))
		a->devidx = alsa_out_conf.idev;

	ffpcm_fmtcopy(&fmt, &d->audio.convfmt);

	if (mod->out_valid) {

		if (mod->usedby != NULL) {
			alsa_out *a = mod->usedby;
			mod->usedby = NULL;
			a->stop = 1;
			alsa_onplay(a);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& fmt.sample_rate == mod->fmt.sample_rate
			&& mod->devidx == a->devidx) {

			ffalsa_stop(&mod->out);
			ffalsa_clear(&mod->out);
			ffalsa_async(&mod->out, 0);
			reused = 1;
			goto fin;
		}

		ffalsa_close(&mod->out);
		ffmem_tzero(&mod->out);
		mod->out_valid = 0;
	}

	if (0 != alsa_devbyidx(&a->dev, a->devidx, FFALSA_DEV_PLAYBACK)) {
		errlog(core, d->trk, "alsa", "no audio device by index #%u", a->devidx);
		goto done;
	}

	mod->out.handler = &alsa_onplay;
	mod->out.autostart = 1;
	if (alsa_out_conf.nfy_rate != 0)
		mod->out.nfy_interval = ffpcm_samples(alsa_out_conf.buflen / alsa_out_conf.nfy_rate, fmt.sample_rate);
	r = ffalsa_open(&mod->out, a->dev.id, &fmt, alsa_out_conf.buflen);

	if (r != 0) {
		errlog(core, d->trk, "alsa", "ffalsa_open(): (%d) %s", r, ffalsa_errstr(r));
		goto done;
	}

	ffalsa_devdestroy(&a->dev);
	mod->out_valid = 1;
	mod->fmt = fmt;
	mod->devidx = a->devidx;

fin:
	mod->out.udata = a;
	mod->usedby = a;
	dbglog(core, d->trk, "alsa", "%s buffer %ums, %uHz"
		, reused ? "reused" : "opened", ffpcm_bytes2time(&fmt, ffalsa_bufsize(&mod->out))
		, fmt.sample_rate);
	return 0;

done:
	return FMED_RERR;
}

static void alsa_onplay(void *udata)
{
	alsa_out *a = udata;
	a->task.handler(a->task.param);
}

static int alsa_write(void *ctx, fmed_filt *d)
{
	alsa_out *a = ctx;
	int r;

	switch (a->state) {
	case I_OPEN:
		d->audio.convfmt.ileaved = 0;
		if (0 != (r = alsa_create(a, d)))
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
		ffalsa_stop(&mod->out);
		ffalsa_clear(&mod->out);
		ffalsa_async(&mod->out, 0);
		a->dataoff = 0;
		return FMED_RMORE;
	}

	if (d->snd_output_pause) {
		d->snd_output_pause = 0;
		d->track->cmd(d->trk, FMED_TRACK_PAUSE);
		ffalsa_stop(&mod->out);
		return FMED_RMORE;
	}

	while (d->datalen != 0) {

		r = ffalsa_write(&mod->out, d->datani, d->datalen, a->dataoff);
		if (r < 0) {
			errlog(core, d->trk, "alsa", "ffalsa_write(): (%d) %s", r, ffalsa_errstr(r));
			goto err;

		} else if (r == 0) {
			ffalsa_async(&mod->out, 1);
			return FMED_RASYNC;
		}

		a->dataoff += r;
		d->datalen -= r;
		dbglog(core, d->trk, "alsa", "written %u bytes (%u%% filled)"
			, r, ffalsa_filled(&mod->out) * 100 / ffalsa_bufsize(&mod->out));
	}

	a->dataoff = 0;

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffalsa_stoplazy(&mod->out);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "alsa", "ffalsa_stoplazy(): (%d) %s", r, ffalsa_errstr(r));
			goto err;
		}

		ffalsa_async(&mod->out, 1);
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;

err:
	ffalsa_close(&mod->out);
	ffmem_tzero(&mod->out);
	mod->out_valid = 0;
	mod->usedby = NULL;
	return FMED_RERR;
}


static int alsa_in_config(ffpars_ctx *ctx)
{
	alsa_in_conf.idev = 0;
	alsa_in_conf.buflen = 500;
	ffpars_setargs(ctx, &alsa_in_conf, alsa_in_conf_args, FFCNT(alsa_in_conf_args));
	return 0;
}

static void* alsa_in_open(fmed_filt *d)
{
	alsa_in *a;
	ffpcm fmt;
	int r, idx;
	ffalsa_dev dev;

	if (NULL == (a = ffmem_tcalloc1(alsa_in)))
		return NULL;
	a->cb.handler = d->handler;
	a->cb.param = d->trk;

	if (FMED_NULL == (idx = (int)d->track->getval(d->trk, "capture_device")))
		idx = alsa_in_conf.idev;
	if (0 != alsa_devbyidx(&dev, idx, FFALSA_DEV_CAPTURE)) {
		errlog(core, d->trk, "alsa", "no audio device by index #%u", idx);
		goto fail;
	}

	ain->snd.handler = &alsa_in_oncapt;
	ain->snd.udata = a;
	ffpcm_fmtcopy(&fmt, &d->audio.fmt);
	r = ffalsa_capt_open(&ain->snd, dev.id, &fmt, alsa_in_conf.buflen);

	ffalsa_devdestroy(&dev);

	if (r != 0) {
		errlog(core, d->trk, "alsa", "ffalsa_capt_open(): (%xu) %s", r, ffalsa_errstr(r));
		goto fail;
	}

	if (0 != (r = ffalsa_start(&ain->snd))) {
		errlog(core, d->trk, "alsa", "ffalsa_start(): (%xu) %s", r, ffalsa_errstr(r));
		goto fail;
	}

	dbglog(core, d->trk, "alsa", "opened capture buffer %ums"
		, ffpcm_bytes2time(&fmt, ffalsa_bufsize(&ain->snd)));
	return a;

fail:
	alsa_in_close(a);
	return NULL;
}

static void alsa_in_close(void *ctx)
{
	alsa_in *a = ctx;
	ffalsa_capt_close(&ain->snd);
	ffmem_free(a);
}

static void alsa_in_oncapt(void *udata)
{
	alsa_in *a = udata;
	a->cb.handler(a->cb.param);
}

static int alsa_in_read(void *ctx, fmed_filt *d)
{
	alsa_in *a = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		ffalsa_stop(&ain->snd);
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = ffalsa_capt_read(&ain->snd, a->bufs, &d->outlen);
	if (r < 0) {
		errlog(core, d->trk, "alsa", "ffalsa_capt_read(): (%xu) %s", r, ffalsa_errstr(r));
		return FMED_RERR;
	} else if (r == 0) {
		ffalsa_async(&ain->snd, 1);
		return FMED_RASYNC;
	}
	d->outni = a->bufs;

	dbglog(core, d->trk, "alsa", "read %L bytes", d->outlen);
	a->total_samps += d->outlen / ain->snd.frsize;
	d->audio.pos = a->total_samps;
	return FMED_ROK;
}
