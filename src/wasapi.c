/** WASAPI input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wasapi.h>
#include <FF/array.h>
#include <FFOS/mem.h>


static const fmed_core *core;
static byte stopping;

typedef struct wasapi_out {
	ffwasapi wa;
	fmed_handler handler;
	void *trk;
	uint latcorr;
	unsigned async :1
		, ileaved :1;
} wasapi_out;

typedef wasapi_out wasapi_in;

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
	case FMED_STOP:
		stopping = 1;
		break;

	case FMED_LISTDEV:
		return wasapi_listdev();
	}
	return 0;
}

static void wasapi_destroy(void)
{
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
	ffpcm fmt;
	int r, idx;
	int64 lowlat;
	ffwas_dev dev;

	w = ffmem_tcalloc1(wasapi_out);
	if (w == NULL)
		return NULL;
	w->handler = d->handler;
	w->trk = d->trk;

	if (FMED_NULL == (idx = (int)d->track->getval(d->trk, "playdev_name")))
		idx = wasapi_out_conf.idev;
	if (0 != wasapi_devbyidx(&dev, idx, FFWAS_DEV_RENDER)) {
		errlog(core, d->trk, "wasapi", "no audio device by index #%u", idx);
		goto done;
	}

	if (wasapi_out_conf.exclusive == EXCL_ALLOWED && FMED_NULL != (lowlat = d->track->getval(d->trk, "low_latency")))
		w->wa.excl = !!lowlat;
	else if (wasapi_out_conf.exclusive == EXCL_ALWAYS)
		w->wa.excl = 1;

	w->wa.handler = &wasapi_onplay;
	w->wa.udata = w;
	w->wa.autostart = 1;
	fmed_getpcm(d, &fmt);
	r = ffwas_open(&w->wa, dev.id, &fmt, wasapi_out_conf.buflen);

	ffwas_devdestroy(&dev);

	if (r != 0) {
		errlog(core, d->trk, "wasapi", "ffwas_open(): (%xu) %s", r, ffwas_errstr(r));
		goto done;
	}

	dbglog(core, d->trk, "wasapi", "opened buffer %ums"
		, ffpcm_time(w->wa.bufsize, fmt.sample_rate));
	return w;

done:
	wasapi_close(w);
	return NULL;
}

static void wasapi_close(void *ctx)
{
	wasapi_out *w = ctx;
	ffwas_close(&w->wa);
	ffmem_free(w);
}

static void wasapi_onplay(void *udata)
{
	wasapi_out *w = udata;
	if (!w->async)
		return;
	w->async = 0;
	w->handler(w->trk);
}

static int wasapi_write(void *ctx, fmed_filt *d)
{
	wasapi_out *w = ctx;
	int r;

	if (!w->ileaved) {
		int il = (int)fmed_getval("pcm_ileaved");
		if (il != 1) {
			fmed_setval("conv_pcm_ileaved", 1);
			return FMED_RMORE;
		}
		w->ileaved = 1;
	}

	while (d->datalen != 0) {

		r = ffwas_write(&w->wa, d->data, d->datalen);
		if (r < 0) {
			errlog(core, d->trk, "wasapi", "ffwas_write(): (%xu) %s", r, ffwas_errstr(r));
			return FMED_RERR;

		} else if (r == 0) {
			w->async = 1;
			return FMED_RASYNC;
		}

		d->data += r;
		d->datalen -= r;
		dbglog(core, d->trk, "wasapi", "written %u bytes (%u%% filled)"
			, r, ffwas_filled(&w->wa) * 100 / ffwas_bufsize(&w->wa));
	}

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffwas_stoplazy(&w->wa);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "wasapi", "ffwas_stoplazy(): (%xu) %s", r, ffwas_errstr(r));
			return FMED_RERR;
		}

		w->async = 1;
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;
}


static int wasapi_in_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &wasapi_in_conf, wasapi_in_conf_args, FFCNT(wasapi_in_conf_args));
	return 0;
}

static void* wasapi_in_open(fmed_filt *d)
{
	wasapi_in *w;
	ffpcm fmt;
	int r, idx;
	ffwas_dev dev;
	int lowlat;

	w = ffmem_tcalloc1(wasapi_in);
	if (w == NULL)
		return NULL;
	w->handler = d->handler;
	w->trk = d->trk;

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

	w->wa.handler = &wasapi_onplay;
	w->wa.udata = w;
	fmt.format = (int)fmed_getval("pcm_format");
	fmt.channels = (int)fmed_getval("pcm_channels");
	fmt.sample_rate = (int)fmed_getval("pcm_sample_rate");
	r = ffwas_capt_open(&w->wa, dev.id, &fmt, wasapi_in_conf.buflen);

	ffwas_devdestroy(&dev);

	if (r != 0) {
		errlog(core, d->trk, "wasapi", "ffwas_capt_open(): (%xu) %s", r, ffwas_errstr(r));
		goto fail;
	}

	r = ffwas_start(&w->wa);
	if (r != 0) {
		errlog(core, d->trk, "wasapi", "ffwas_start(): (%xu) %s", r, ffwas_errstr(r));
		goto fail;
	}

	dbglog(core, d->trk, "wasapi", "opened capture buffer %ums"
		, ffpcm_bytes2time(&fmt, w->wa.bufsize));

	if (wasapi_in_conf.latency_autocorrect)
		w->latcorr = ffpcm_samples(wasapi_out_conf.buflen, fmt.sample_rate) * ffpcm_size1(&fmt)
			+ w->wa.bufsize;
	return w;

fail:
	wasapi_in_close(w);
	return NULL;
}

static void wasapi_in_close(void *ctx)
{
	wasapi_in *w = ctx;
	ffwas_capt_close(&w->wa);
	ffmem_free(w);
}

static int wasapi_in_read(void *ctx, fmed_filt *d)
{
	wasapi_in *w = ctx;
	int r = ffwas_capt_read(&w->wa, (void**)&d->out, &d->outlen);
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

	if (stopping) {
		ffwas_stop(&w->wa);
		return FMED_RDONE;
	}
	return FMED_ROK;
}
