/** WASAPI input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wasapi.h>
#include <FF/array.h>
#include <FFOS/mem.h>


static const fmed_core *core;

typedef struct wasapi_out wasapi_out;

typedef struct wasapi_mod {
	ffwasapi out;
	ffpcm fmt;
	wasapi_out *usedby;
	const fmed_track *track;
	uint devidx;
	uint out_valid :1;
} wasapi_mod;

static wasapi_mod *mod;

struct wasapi_out {
	uint state;

	ffwas_dev dev;
	uint devidx;

	fftask task;
	unsigned stop :1;
};

enum { WAS_TRYOPEN, WAS_OPEN, WAS_DATA };

typedef struct wasapi_in {
	ffwasapi wa;
	uint latcorr;
	fftask task;
	uint64 total_samps;
	unsigned async :1;
} wasapi_in;

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
static int wasapi_sig(uint signo);
static void wasapi_destroy(void);
static const fmed_mod fmed_wasapi_mod = {
	&wasapi_iface, &wasapi_sig, &wasapi_destroy
};

static int wasapi_listdev(void);

//OUTPUT
static void* wasapi_open(fmed_filt *d);
static int wasapi_write(void *ctx, fmed_filt *d);
static void wasapi_close(void *ctx);
static int wasapi_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_wasapi_out = {
	&wasapi_open, &wasapi_write, &wasapi_close, &wasapi_out_config
};

static const ffpars_arg wasapi_out_conf_args[] = {
	{ "device_index",  FFPARS_TINT,  FFPARS_DSTOFF(struct wasapi_out_conf_t, idev) }
	, { "buffer_length",  FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct wasapi_out_conf_t, buflen) }
	, { "exclusive_mode",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(struct wasapi_out_conf_t, exclusive) }
};

//INPUT
static void* wasapi_in_open(fmed_filt *d);
static int wasapi_in_read(void *ctx, fmed_filt *d);
static void wasapi_in_close(void *ctx);
static int wasapi_in_config(ffpars_ctx *ctx);
static const fmed_filter fmed_wasapi_in = {
	&wasapi_in_open, &wasapi_in_read, &wasapi_in_close, &wasapi_in_config
};

static void wasapi_onplay(void *udata);
static void wasapi_oncapt(void *udata);

static const ffpars_arg wasapi_in_conf_args[] = {
	{ "device_index",  FFPARS_TINT,  FFPARS_DSTOFF(struct wasapi_in_conf_t, idev) }
	, { "buffer_length",  FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct wasapi_in_conf_t, buflen) }
	, { "latency_autocorrect",  FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct wasapi_in_conf_t, latency_autocorrect) }
	, { "exclusive_mode",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(struct wasapi_in_conf_t, exclusive) }
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	ffwas_init();
	core = _core;
	return &fmed_wasapi_mod;
}


static const void* wasapi_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		wasapi_out_conf.idev = 0;
		wasapi_out_conf.exclusive = EXCL_DISABLED;
		wasapi_out_conf.buflen = 500;
		return &fmed_wasapi_out;

	} else if (!ffsz_cmp(name, "in")) {
		wasapi_in_conf.idev = 0;
		wasapi_in_conf.exclusive = EXCL_DISABLED;
		wasapi_in_conf.latency_autocorrect = 0;
		wasapi_in_conf.buflen = 100;
		return &fmed_wasapi_in;
	}
	return NULL;
}

static int wasapi_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (mod = ffmem_tcalloc1(wasapi_mod)))
			return -1;
		mod->track = core->getmod("#core.track");
		return 0;

	case FMED_LISTDEV:
		return wasapi_listdev();
	}
	return 0;
}

static void wasapi_destroy(void)
{
	if (mod != NULL) {
		if (mod->out_valid)
			ffwas_close(&mod->out);
		ffmem_free(mod);
		mod = NULL;
	}

	ffwas_uninit();
}

static int wasapi_listdev(void)
{
	ffwas_dev d;
	int i, r;
	ffstr3 buf = {0};

	ffwas_devinit(&d);
	ffstr_catfmt(&buf, "Playback:\n");
	for (i = 1;  0 == (r = ffwas_devnext(&d, FFWAS_DEV_RENDER));  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d.name);
	}
	ffwas_devdestroy(&d);
	if (r < 0)
		goto fail;

	ffwas_devinit(&d);
	ffstr_catfmt(&buf, "\nCapture:\n");
	for (i = 1;  0 == (r = ffwas_devnext(&d, FFWAS_DEV_CAPTURE));  i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d.name);
	}
	ffwas_devdestroy(&d);
	if (r < 0)
		goto fail;

	fffile_write(ffstdout, buf.ptr, buf.len);
	ffarr_free(&buf);
	return 1;

fail:
	errlog(core, NULL, "wasapi", "ffwas_devnext(): (%xu) %s", r, ffwas_errstr(r));
	ffarr_free(&buf);
	return -1;
}


static int wasapi_devbyidx(ffwas_dev *d, uint idev, uint flags)
{
	ffwas_devinit(d);
	for (;  idev != 0;  idev--) {
		int r = ffwas_devnext(d, flags);
		if (r != 0) {
			ffwas_devdestroy(d);
			return r;
		}
	}
	return 0;
}

static int wasapi_out_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &wasapi_out_conf, wasapi_out_conf_args, FFCNT(wasapi_out_conf_args));
	return 0;
}

static void* wasapi_open(fmed_filt *d)
{
	wasapi_out *w;

	w = ffmem_tcalloc1(wasapi_out);
	if (w == NULL)
		return NULL;
	w->task.handler = d->handler;
	w->task.param = d->trk;
	return w;
}

static int wasapi_create(wasapi_out *w, fmed_filt *d)
{
	ffpcm fmt, in_fmt;
	int r, reused = 0, excl = 0;
	int64 lowlat;

	if (w->state == WAS_TRYOPEN
		&& FMED_NULL == (int)(w->devidx = (int)d->track->getval(d->trk, "playdev_name")))
		w->devidx = wasapi_out_conf.idev;

	fmt.format = fmed_getval("pcm_format");
	fmt.channels = fmed_getval("pcm_channels");
	fmt.sample_rate = fmed_getval("pcm_sample_rate");

	if (wasapi_out_conf.exclusive == EXCL_ALLOWED && FMED_NULL != (lowlat = d->track->getval(d->trk, "low_latency")))
		excl = !!lowlat;
	else if (wasapi_out_conf.exclusive == EXCL_ALWAYS)
		excl = 1;

	if (mod->out_valid) {

		if (mod->usedby != NULL) {
			wasapi_out *w = mod->usedby;
			mod->usedby = NULL;
			w->stop = 1;
			wasapi_onplay(w);
		}

		if (fmt.channels == mod->fmt.channels
			&& fmt.format == mod->fmt.format
			&& (!excl || fmt.sample_rate == mod->fmt.sample_rate)
			&& mod->devidx == w->devidx && mod->out.excl == excl) {

			if (!excl && fmt.sample_rate != mod->fmt.sample_rate) {
				fmt.sample_rate = mod->fmt.sample_rate;
				fmed_setval("conv_pcm_rate", mod->fmt.sample_rate);
			}

			ffwas_stop(&mod->out);
			ffwas_clear(&mod->out);
			ffwas_async(&mod->out, 0);
			reused = 1;
			goto fin;
		}

		ffwas_close(&mod->out);
		ffmem_tzero(&mod->out);
		mod->out_valid = 0;
	}

	if (w->state == WAS_TRYOPEN
		&& 0 != wasapi_devbyidx(&w->dev, w->devidx, FFWAS_DEV_RENDER)) {
		errlog(core, d->trk, "wasapi", "no audio device by index #%u", w->devidx);
		goto done;
	}

	mod->out.excl = excl;

	mod->out.handler = &wasapi_onplay;
	mod->out.autostart = 1;
	in_fmt = fmt;
	r = ffwas_open(&mod->out, w->dev.id, &fmt, wasapi_out_conf.buflen);

	if (r != 0) {

		if (r == AUDCLNT_E_UNSUPPORTED_FORMAT && w->state == WAS_TRYOPEN
			&& memcmp(&fmt, &in_fmt, sizeof(fmt))) {

			if (fmt.format != in_fmt.format)
				fmed_setval("conv_pcm_format", fmt.format);

			if (fmt.sample_rate != in_fmt.sample_rate)
				fmed_setval("conv_pcm_rate", fmt.sample_rate);

			if (fmt.channels != in_fmt.channels)
				fmed_setval("conv_channels", fmt.channels);
			w->state = WAS_OPEN;
			return FMED_RMORE;
		}

		errlog(core, d->trk, "wasapi", "ffwas_open(): (%xu) %s", r, ffwas_errstr(r));
		goto done;
	}

	ffwas_devdestroy(&w->dev);
	mod->out_valid = 1;
	mod->fmt = fmt;
	mod->devidx = w->devidx;

fin:
	mod->out.udata = w;
	mod->usedby = w;
	dbglog(core, d->trk, "wasapi", "%s buffer %ums, %uHz, excl:%u"
		, reused ? "reused" : "opened", ffpcm_bytes2time(&fmt, ffwas_bufsize(&mod->out))
		, fmt.sample_rate, mod->out.excl);
	return 0;

done:
	return FMED_RERR;
}

static void wasapi_close(void *ctx)
{
	wasapi_out *w = ctx;
	if (mod->usedby == w) {
		void *trk = w->task.param;
		if (FMED_NULL != mod->track->getval(trk, "stopped")) {
			ffwas_close(&mod->out);
			ffmem_tzero(&mod->out);
			mod->out_valid = 0;
		} else {
			ffwas_stop(&mod->out);
			ffwas_clear(&mod->out);
			ffwas_async(&mod->out, 0);
		}
		mod->usedby = NULL;
	}
	ffwas_devdestroy(&w->dev);
	core->task(&w->task, FMED_TASK_DEL);
	ffmem_free(w);
}

static void wasapi_onplay(void *udata)
{
	wasapi_out *w = udata;
	core->task(&w->task, FMED_TASK_POST);
}

static int wasapi_write(void *ctx, fmed_filt *d)
{
	wasapi_out *w = ctx;
	int r;

	switch (w->state) {
	case WAS_TRYOPEN:
		if (1 != fmed_getval("pcm_ileaved"))
			fmed_setval("conv_pcm_ileaved", 1);
		// break

	case WAS_OPEN:
		if (0 != (r = wasapi_create(w, d)))
			return r;
		w->state = WAS_DATA;
		return FMED_RMORE;

	case WAS_DATA:
		break;
	}

	if (w->stop || (d->flags & FMED_FSTOP)) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if (1 == d->track->popval(d->trk, "snd_output_clear")) {
		ffwas_stop(&mod->out);
		ffwas_clear(&mod->out);
		ffwas_async(&mod->out, 0);
		return FMED_RMORE;
	}

	if (1 == d->track->popval(d->trk, "snd_output_pause")) {
		ffwas_stop(&mod->out);
		return FMED_RMORE;
	}

	while (d->datalen != 0) {

		r = ffwas_write(&mod->out, d->data, d->datalen);
		if (r < 0) {
			errlog(core, d->trk, "wasapi", "ffwas_write(): (%xu) %s", r, ffwas_errstr(r));
			goto err;

		} else if (r == 0) {
			ffwas_async(&mod->out, 1);
			return FMED_RASYNC;
		}

		d->data += r;
		d->datalen -= r;
		dbglog(core, d->trk, "wasapi", "written %u bytes (%u%% filled)"
			, r, ffwas_filled(&mod->out) * 100 / ffwas_bufsize(&mod->out));
	}

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffwas_stoplazy(&mod->out);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "wasapi", "ffwas_stoplazy(): (%xu) %s", r, ffwas_errstr(r));
			goto err;
		}

		ffwas_async(&mod->out, 1);
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;

err:
	ffwas_close(&mod->out);
	ffmem_tzero(&mod->out);
	mod->out_valid = 0;
	mod->usedby = NULL;
	return FMED_RERR;
}


static int wasapi_in_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &wasapi_in_conf, wasapi_in_conf_args, FFCNT(wasapi_in_conf_args));
	return 0;
}

static void* wasapi_in_open(fmed_filt *d)
{
	wasapi_in *w;
	ffpcm fmt, in_fmt;
	int r, idx;
	ffwas_dev dev = {0};
	int lowlat;
	ffbool try_open = 1;

	w = ffmem_tcalloc1(wasapi_in);
	if (w == NULL)
		return NULL;
	w->task.handler = d->handler;
	w->task.param = d->trk;

	if (FMED_NULL == (idx = (int)d->track->getval(d->trk, "capture_device")))
		idx = wasapi_in_conf.idev;
	if (0 != wasapi_devbyidx(&dev, idx, FFWAS_DEV_CAPTURE)) {
		errlog(core, d->trk, "wasapi", "no audio device by index #%u", idx);
		goto fail;
	}

	if (wasapi_in_conf.exclusive == EXCL_ALLOWED && FMED_NULL != (lowlat = (int)fmed_getval("low_latency")))
		w->wa.excl = !!lowlat;
	else if (wasapi_in_conf.exclusive == EXCL_ALWAYS)
		w->wa.excl = 1;

	w->wa.handler = &wasapi_oncapt;
	w->wa.udata = w;
	fmt.format = (int)fmed_getval("pcm_format");
	fmt.channels = (int)fmed_getval("pcm_channels");
	fmt.sample_rate = (int)fmed_getval("pcm_sample_rate");
	in_fmt = fmt;

again:
	r = ffwas_capt_open(&w->wa, dev.id, &fmt, wasapi_in_conf.buflen);

	if (r != 0) {

		if (r == AUDCLNT_E_UNSUPPORTED_FORMAT && try_open
			&& memcmp(&fmt.format, &in_fmt, sizeof(fmt))) {

			if (fmt.format != in_fmt.format)
				fmed_setval("pcm_format", fmt.format);

			if (fmt.sample_rate != in_fmt.sample_rate) {
				d->track->setval4(d->trk, "conv_pcm_rate", in_fmt.sample_rate, FMED_TRK_FNO_OVWRITE);
				fmed_setval("pcm_sample_rate", fmt.sample_rate);
			}

			if (fmt.channels != in_fmt.channels)
				fmed_setval("pcm_channels", fmt.channels);

			try_open = 0;
			goto again;
		}

		errlog(core, d->trk, "wasapi", "ffwas_capt_open(): (%xu) %s", r, ffwas_errstr(r));
		goto fail;
	}

	r = ffwas_start(&w->wa);
	if (r != 0) {
		errlog(core, d->trk, "wasapi", "ffwas_start(): (%xu) %s", r, ffwas_errstr(r));
		goto fail;
	}

	dbglog(core, d->trk, "wasapi", "opened capture buffer %ums"
		, ffpcm_bytes2time(&fmt, ffwas_bufsize(&mod->out)));

	if (wasapi_in_conf.latency_autocorrect)
		w->latcorr = ffpcm_samples(wasapi_out_conf.buflen, fmt.sample_rate) * ffpcm_size1(&fmt)
			+ w->wa.bufsize;

	ffwas_devdestroy(&dev);
	fmed_setval("pcm_ileaved", 1);
	return w;

fail:
	ffwas_devdestroy(&dev);
	wasapi_in_close(w);
	return NULL;
}

static void wasapi_in_close(void *ctx)
{
	wasapi_in *w = ctx;
	core->task(&w->task, FMED_TASK_DEL);
	ffwas_capt_close(&w->wa);
	ffmem_free(w);
}

static void wasapi_oncapt(void *udata)
{
	wasapi_in *w = udata;
	if (!w->async)
		return;
	w->async = 0;
	core->task(&w->task, FMED_TASK_POST);
}

static int wasapi_in_read(void *ctx, fmed_filt *d)
{
	wasapi_in *w = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		ffwas_stop(&w->wa);
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = ffwas_capt_read(&w->wa, (void**)&d->out, &d->outlen);
	if (r < 0) {
		errlog(core, d->trk, "wasapi", "ffwas_capt_read(): (%xu) %s", r, ffwas_errstr(r));
		return FMED_RERR;
	}
	if (r == 0) {
		w->async = 1;
		return FMED_RASYNC;
	}

	dbglog(core, d->trk, "wasapi", "read %L bytes", d->outlen);

	if (w->latcorr != 0) {
		uint n = (uint)ffmin(w->latcorr, d->outlen);
		d->out += n;
		d->outlen -= n;
		w->latcorr -= n;
	}

	w->total_samps += d->outlen / w->wa.frsize;
	fmed_setval("current_position", w->total_samps);
	return FMED_ROK;
}
