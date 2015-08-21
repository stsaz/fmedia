/** Direct Sound input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/dsound.h>
#include <FF/array.h>
#include <FFOS/mem.h>


static const fmed_core *core;
static byte refcount;

typedef struct dsnd_out {
	ffdsnd_buf snd;
	fftask task;
	unsigned async :1
		, ileaved :1;
} dsnd_out;

typedef struct dsnd_in {
	ffdsnd_capt snd;
	fftask task;
	unsigned async :1;
} dsnd_in;

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
static int dsnd_sig(uint signo);
static void dsnd_destroy(void);
static const fmed_mod fmed_dsnd_mod = {
	&dsnd_iface, &dsnd_sig, &dsnd_destroy
};

static int dsnd_listdev(void);

//OUTPUT
static void* dsnd_open(fmed_filt *d);
static int dsnd_write(void *ctx, fmed_filt *d);
static void dsnd_close(void *ctx);
static int dsnd_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_dsnd_out = {
	&dsnd_open, &dsnd_write, &dsnd_close, &dsnd_out_config
};

static const ffpars_arg dsnd_out_conf_args[] = {
	{ "device_index",  FFPARS_TINT,  FFPARS_DSTOFF(struct dsnd_out_conf_t, idev) }
	, { "buffer_length",  FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct dsnd_out_conf_t, buflen) }
};

static void dsnd_onplay(void *udata);

//INPUT
static void* dsnd_in_open(fmed_filt *d);
static int dsnd_in_read(void *ctx, fmed_filt *d);
static void dsnd_in_close(void *ctx);
static int dsnd_in_config(ffpars_ctx *ctx);
static const fmed_filter fmed_dsnd_in = {
	&dsnd_in_open, &dsnd_in_read, &dsnd_in_close, &dsnd_in_config
};

static void dsnd_in_onplay(void *udata);

static const ffpars_arg dsnd_in_conf_args[] = {
	{ "device_index",  FFPARS_TINT,  FFPARS_DSTOFF(struct dsnd_in_conf_t, idev) }
	, { "buffer_length",  FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct dsnd_in_conf_t, buflen) }
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	if (refcount++ == 0 && 0 != ffdsnd_init())
		return NULL;
	return &fmed_dsnd_mod;
}


static const void* dsnd_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		dsnd_out_conf.idev = 0;
		dsnd_out_conf.buflen = 500;
		return &fmed_dsnd_out;

	} else if (!ffsz_cmp(name, "in")) {
		dsnd_in_conf.idev = 0;
		dsnd_in_conf.buflen = 500;
		return &fmed_dsnd_in;
	}
	return NULL;
}

static int dsnd_sig(uint signo)
{
	switch (signo) {
	case FMED_LISTDEV:
		return dsnd_listdev();
	}
	return 0;
}

static void dsnd_destroy(void)
{
	if (--refcount == 0)
		ffdsnd_uninit();
}

static int dsnd_listdev(void)
{
	struct ffdsnd_devenum *d, *dhead;
	int i, r;
	ffstr3 buf = {0};

	if (0 != (r = ffdsnd_devenum(&dhead, FFDSND_DEV_RENDER)))
		goto fail;
	ffstr_catfmt(&buf, "Playback:\n");
	for (d = dhead, i = 0;  d != NULL; d = d->next, i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d->name);
	}
	ffdsnd_devenumfree(dhead);

	if (0 != (r = ffdsnd_devenum(&dhead, FFDSND_DEV_CAPTURE)))
		goto fail;
	ffstr_catfmt(&buf, "\nCapture:\n");
	for (d = dhead, i = 0;  d != NULL; d = d->next, i++) {
		ffstr_catfmt(&buf, "device #%u: %s\n", i, d->name);
	}
	ffdsnd_devenumfree(dhead);

	fffile_write(ffstdout, buf.ptr, buf.len);
	ffarr_free(&buf);
	return 1;

fail:
	errlog(core, NULL, "dsound", "ffdsnd_devenum(): (%xu) %s", r, ffdsnd_errstr(r));
	ffarr_free(&buf);
	return -1;
}

/** Get device by index. */
static int dsnd_devbyidx(struct ffdsnd_devenum **dhead, struct ffdsnd_devenum **dev, uint idev, uint flags)
{
	struct ffdsnd_devenum *devs, *d;

	if (0 != ffdsnd_devenum(&devs, flags))
		return 1;

	for (d = devs;  d != NULL;  d = d->next, idev--) {
		if (idev == 0) {
			*dev = d;
			*dhead = devs;
			return 0;
		}
	}

	ffdsnd_devenumfree(devs);
	return 1;
}

static void dsnd_onplay(void *udata)
{
	dsnd_out *ds = udata;
	if (!ds->async)
		return; //the function may be called when we aren't expecting it
	ds->async = 0;
	core->task(&ds->task, FMED_TASK_POST);
}

static int dsnd_out_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &dsnd_out_conf, dsnd_out_conf_args, FFCNT(dsnd_out_conf_args));
	return 0;
}

static void* dsnd_open(fmed_filt *d)
{
	dsnd_out *ds;
	ffpcm fmt;
	int e, idx;
	struct ffdsnd_devenum *dhead, *dev;

	ds = ffmem_tcalloc1(dsnd_out);
	if (ds == NULL)
		return NULL;
	ds->task.handler = d->handler;
	ds->task.param = d->trk;

	if (FMED_NULL == (idx = (int)d->track->getval(d->trk, "playdev_name")))
		idx = dsnd_out_conf.idev;
	if (0 != dsnd_devbyidx(&dhead, &dev, idx, FFDSND_DEV_RENDER)) {
		errlog(core, d->trk, "dsound", "no audio device by index #%u", idx);
		goto done;
	}

	ds->snd.handler = &dsnd_onplay;
	ds->snd.udata = ds;
	fmt.format = (int)d->track->getval(d->trk, "pcm_format");
	fmt.channels = (int)d->track->getval(d->trk, "pcm_channels");
	fmt.sample_rate = (int)d->track->getval(d->trk, "pcm_sample_rate");
	e = ffdsnd_open(&ds->snd, dev->id, &fmt, dsnd_out_conf.buflen);

	ffdsnd_devenumfree(dhead);

	if (e != 0) {
		errlog(core, d->trk, "dsound", "ffdsnd_open(): (%xu) %s", e, ffdsnd_errstr(e));
		goto done;
	}

	dbglog(core, d->trk, "dsound", "opened buffer %u bytes", ds->snd.bufsize);
	return ds;

done:
	dsnd_close(ds);
	return NULL;
}

static void dsnd_close(void *ctx)
{
	dsnd_out *ds = ctx;

	core->task(&ds->task, FMED_TASK_DEL);
	ffdsnd_close(&ds->snd);
	ffmem_free(ds);
}

static int dsnd_write(void *ctx, fmed_filt *d)
{
	dsnd_out *ds = ctx;
	int r;

	if (!ds->ileaved) {
		int il = (int)fmed_getval("pcm_ileaved");
		if (il != 1) {
			fmed_setval("conv_pcm_ileaved", 1);
			return FMED_RMORE;
		}
		ds->ileaved = 1;
	}

	if (d->datalen != 0) {
		r = ffdsnd_write(&ds->snd, d->data, d->datalen);
		if (r < 0) {
			errlog(core, d->trk, "dsound", "ffdsnd_write(): (%xu) %s", r, ffdsnd_errstr(r));
			return r;
		}

		dbglog(core, d->trk, "dsound", "written %u bytes (%u%% filled)"
			, r, ffdsnd_filled(&ds->snd) * 100 / ds->snd.bufsize);

		d->data += r;
		d->datalen -= r;
	}

	if ((d->flags & FMED_FLAST) && d->datalen == 0) {

		r = ffdsnd_stoplazy(&ds->snd);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog(core, d->trk,  "dsound", "ffdsnd_stoplazy(): (%xu) %s", r, ffdsnd_errstr(r));
			return FMED_RERR;
		}

		ds->async = 1;
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	if (d->datalen != 0) {
		ffdsnd_start(&ds->snd);
		ds->async = 1;
		return FMED_RASYNC; //sound buffer is full
	}

	return FMED_ROK; //more data may be written into the sound buffer
}


static int dsnd_in_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &dsnd_in_conf, dsnd_in_conf_args, FFCNT(dsnd_in_conf_args));
	return 0;
}

static void* dsnd_in_open(fmed_filt *d)
{
	dsnd_in *ds;
	ffpcm fmt;
	int r, idx;
	struct ffdsnd_devenum *dhead, *dev;

	ds = ffmem_tcalloc1(dsnd_in);
	if (ds == NULL)
		return NULL;
	ds->task.handler = d->handler;
	ds->task.param = d->trk;

	if (FMED_NULL == (idx = (int)d->track->getval(d->trk, "capture_device")))
		idx = dsnd_in_conf.idev;
	if (0 != dsnd_devbyidx(&dhead, &dev, idx, FFDSND_DEV_CAPTURE)) {
		errlog(core, d->trk, "dsound", "no audio device by index #%u", idx);
		goto fail;
	}

	ds->snd.handler = &dsnd_in_onplay;
	ds->snd.udata = ds;
	fmt.format = (int)d->track->getval(d->trk, "pcm_format");
	fmt.channels = (int)d->track->getval(d->trk, "pcm_channels");
	fmt.sample_rate = (int)d->track->getval(d->trk, "pcm_sample_rate");
	r = ffdsnd_capt_open(&ds->snd, dev->id, &fmt, dsnd_in_conf.buflen);

	ffdsnd_devenumfree(dhead);

	if (r != 0) {
		errlog(core, d->trk, "dsound", "ffdsnd_capt_open(): %d", r);
		goto fail;
	}

	r = ffdsnd_capt_start(&ds->snd);
	if (r != 0) {
		errlog(core, d->trk, "dsound", "ffdsnd_capt_start(): %d", r);
		goto fail;
	}

	dbglog(core, d->trk, "dsound", "opened capture buffer %u bytes", ds->snd.bufsize);
	return ds;

fail:
	dsnd_in_close(ds);
	return NULL;
}

static void dsnd_in_close(void *ctx)
{
	dsnd_in *ds = ctx;

	core->task(&ds->task, FMED_TASK_DEL);
	ffdsnd_capt_close(&ds->snd);
	ffmem_free(ds);
}

static void dsnd_in_onplay(void *udata)
{
	dsnd_in *ds = udata;
	if (!ds->async)
		return;
	ds->async = 0;
	core->task(&ds->task, FMED_TASK_POST);
}

static int dsnd_in_read(void *ctx, fmed_filt *d)
{
	dsnd_in *ds = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		ffdsnd_capt_stop(&ds->snd);
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = ffdsnd_capt_read(&ds->snd, (void**)&d->out, &d->outlen);
	if (r < 0) {
		errlog(core, d->trk, "dsound", "ffdsnd_capt_read(): (%xu) %s", r, ffdsnd_errstr(r));
		return FMED_RERR;
	}
	if (r == 0) {
		ds->async = 1;
		return FMED_RASYNC;
	}

	dbglog(core, d->trk, "dsound", "read %L bytes", d->outlen);
	return FMED_ROK;
}
