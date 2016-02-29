/** ALSA input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/alsa.h>


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

	fftask task;
	uint stop :1;
};

enum { I_OPEN, I_DATA };

static struct alsa_out_conf_t {
	uint idev;
	uint buflen;
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
};

static void alsa_onplay(void *udata);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	ffalsa_init();
	core = _core;
	return &fmed_alsa_mod;
}


static const void* alsa_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		alsa_out_conf.idev = 0;
		alsa_out_conf.buflen = 500;
		return &fmed_alsa_out;
	}
	return NULL;
}

static int alsa_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_tcalloc1(alsa_mod)))
			return -1;
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

	ffalsa_uninit();
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
	core->task(&a->task, FMED_TASK_DEL);
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

	if (FMED_NULL == (a->devidx = (int)d->track->getval(d->trk, "playdev_name")))
		a->devidx = alsa_out_conf.idev;

	fmt.format = FFPCM_16LE;
	fmt.channels = fmed_getval("pcm_channels");
	fmt.sample_rate = fmed_getval("pcm_sample_rate");

	if (mod->out_valid) {

		if (mod->usedby != NULL) {
			alsa_out *a = mod->usedby;
			mod->usedby = NULL;
			a->stop = 1;
			alsa_onplay(a);
		}

		if (fmt.channels == mod->fmt.channels
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
	core->task(&a->task, FMED_TASK_POST);
}

static int alsa_write(void *ctx, fmed_filt *d)
{
	alsa_out *a = ctx;
	int r;

	switch (a->state) {
	case I_OPEN:
		fmed_setval("conv_pcm_format", FFPCM_16LE);
		if (1 == fmed_getval("pcm_ileaved"))
			fmed_setval("conv_pcm_ileaved", 0);
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

	if (1 == d->track->popval(d->trk, "snd_output_clear")) {
		ffalsa_stop(&mod->out);
		ffalsa_clear(&mod->out);
		ffalsa_async(&mod->out, 0);
		a->dataoff = 0;
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
