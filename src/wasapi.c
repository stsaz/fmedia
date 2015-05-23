/** WASAPI output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wasapi.h>
#include <FF/array.h>
#include <FFOS/mem.h>


static const fmed_core *core;

typedef struct wasapi_out {
	ffwasapi wa;
	fmed_handler handler;
	void *trk;
	unsigned async :1
		, silence :1;
} wasapi_out;

enum {
	conf_exclusive = 1 //0: disabled, 1: allowed, 2: always
	, conf_buflen = 1000 //desirable buffer length, in msec
};

//FMEDIA MODULE
static const fmed_filter* wasapi_iface(const char *name);
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
static const fmed_filter fmed_wasapi_out = {
	&wasapi_open, &wasapi_write, &wasapi_close
};

static void wasapi_onplay(void *udata);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	ffwas_init();
	core = _core;
	return &fmed_wasapi_mod;
}


static const fmed_filter* wasapi_iface(const char *name)
{
	if (!ffsz_cmp(name, "out"))
		return &fmed_wasapi_out;
	return NULL;
}

static int wasapi_sig(uint signo)
{
	switch (signo) {
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

	idx = (int)d->track->getval(d->trk, "playdev_name");
	if (0 != wasapi_devbyidx(&dev, idx, FFWAS_DEV_RENDER)) {
		errlog(core, d->trk, "wasapi", "no audio device by index #%u", idx);
		goto done;
	}

	if (conf_exclusive == 1 && FMED_NULL != (lowlat = d->track->getval(d->trk, "low_latency")))
		w->wa.excl = !!lowlat;
	else if (conf_exclusive == 2)
		w->wa.excl = 1;

	w->wa.handler = &wasapi_onplay;
	w->wa.udata = w;
	w->wa.autostart = 1;
	fmt.format = (int)d->track->getval(d->trk, "pcm_format");
	fmt.channels = (int)d->track->getval(d->trk, "pcm_channels");
	fmt.sample_rate = (int)d->track->getval(d->trk, "pcm_sample_rate");
	r = ffwas_open(&w->wa, dev.id, &fmt, conf_buflen);

	ffwas_devdestroy(&dev);

	if (r != 0) {
		errlog(core, d->trk, "wasapi", "ffwas_open(): (%xu) %s", r, ffwas_errstr(r));
		goto done;
	}

	dbglog(core, d->trk, "wasapi", "opened buffer");
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

		if (!w->silence) {
			w->silence = 1;
			if (0 > ffwas_silence(&w->wa))
				return FMED_RERR;
		}

		if (0 == ffwas_filled(&w->wa))
			return FMED_RDONE;

		w->async = 1;
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_ROK;
}
