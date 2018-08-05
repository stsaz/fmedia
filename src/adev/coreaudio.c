/** CoreAudio output.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>

#include <FF/adev/coreaudio.h>


#undef errlog
#undef dbglog
#define errlog(...)  fmed_errlog(core, NULL, "coraud", __VA_ARGS__)
#define dbglog(...)  fmed_dbglog(core, NULL, "coraud", __VA_ARGS__)


static const fmed_core *core;

//FMEDIA MODULE
static const void* coraud_iface(const char *name);
static int coraud_conf(const char *name, ffpars_ctx *ctx);
static int coraud_sig(uint signo);
static void coraud_destroy(void);
static const fmed_mod fmed_coraud_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = &coraud_iface,
	.sig = &coraud_sig,
	.destroy = &coraud_destroy,
	.conf = &coraud_conf,
};

//OUTPUT
static void* coraud_open(fmed_filt *d);
static int coraud_write(void *ctx, fmed_filt *d);
static void coraud_close(void *ctx);
static int coraud_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_coraud_out = {
	&coraud_open, &coraud_write, &coraud_close
};

struct coraud_out_conf_t {
	uint idev;
	uint buflen;
};
static struct coraud_out_conf_t coraud_out_conf;

static const ffpars_arg coraud_out_conf_args[] = {
	{ "device_index",	FFPARS_TINT,  FFPARS_DSTOFF(struct coraud_out_conf_t, idev) },
};

//ADEV
static int coraud_adev_list(fmed_adev_ent **ents, uint flags);
static void coraud_adev_listfree(fmed_adev_ent *ents);
static const fmed_adev fmed_coraud_adev = {
	.list = &coraud_adev_list,
	.listfree = &coraud_adev_listfree,
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_coraud_mod;
}


static const void* coraud_iface(const char *name)
{
	if (!ffsz_cmp(name, "out")) {
		return &fmed_coraud_out;
	} else if (!ffsz_cmp(name, "adev")) {
		return &fmed_coraud_adev;
	}
	return NULL;
}

static int coraud_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "out"))
		return coraud_out_config(ctx);
	return -1;
}

static int coraud_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		return 0;
	}
	return 0;
}

static void coraud_destroy(void)
{
}


static int coraud_adev_list(fmed_adev_ent **ents, uint flags)
{
	ffarr o = {0};
	ffcoraud_dev d;
	uint f = 0;
	fmed_adev_ent *e;
	int r, rr = -1;

	ffcoraud_devinit(&d);

	if (flags == FMED_ADEV_PLAYBACK)
		f = FFCORAUD_DEV_PLAYBACK;
	else if (flags == FMED_ADEV_CAPTURE)
		f = FFCORAUD_DEV_CAPTURE;

	for (;;) {
		r = ffcoraud_devnext(&d, f);
		if (r == 1)
			break;
		else if (r < 0) {
			errlog("ffcoraud_devnext(): (%d) %s", r, ffcoraud_errstr(r));
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
	ffcoraud_devdestroy(&d);
	if (rr < 0) {
		FFARR_WALKT(&o, e, fmed_adev_ent) {
			ffmem_safefree(e->name);
		}
		ffarr_free(&o);
	}
	return rr;
}

static void coraud_adev_listfree(fmed_adev_ent *ents)
{
	fmed_adev_ent *e;
	for (e = ents;  e->name != NULL;  e++) {
		ffmem_free(e->name);
	}
	ffmem_free(ents);
}


static int coraud_out_config(ffpars_ctx *ctx)
{
	coraud_out_conf.idev = 0;
	ffpars_setargs(ctx, &coraud_out_conf, coraud_out_conf_args, FFCNT(coraud_out_conf_args));
	return 0;
}

struct coraud_out {
	uint state;
	ffcoraud_buf out;
};

static void* coraud_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog("unsupported input data type: %s", d->datatype);
		return NULL;
	}

	struct coraud_out *a;
	if (NULL == (a = ffmem_new(struct coraud_out)))
		return NULL;
	// a->task.handler = d->handler;
	// a->task.param = d->trk;
	return a;
}

static void coraud_close(void *ctx)
{
	struct coraud_out *a = ctx;
	ffcoraud_close(&a->out);
	ffmem_free(a);
}

static int coraud_create(struct coraud_out *a, fmed_filt *d)
{
	int r, dev_id = -1;
	ffpcm fmt, in_fmt;

	if (FMED_NULL != (int)(dev_id = (int)d->track->getval(d->trk, "playdev_name")))
		return FMED_RERR;

	ffpcm_fmtcopy(&fmt, d->audio.convfmt);

	dbglog("opening device \"%d\", %s/%u/%u"
		, dev_id, ffpcm_fmtstr(fmt.format), fmt.sample_rate, fmt.channels);

	in_fmt = fmt;
	r = ffcoraud_open(&a->out, dev_id, &fmt, 500);

	if (r == FFCORAUD_EFMT && a->state == 0) {
		if (!!ffmemcmp(&fmt, &in_fmt, sizeof(ffpcm))) {

			if (fmt.format != in_fmt.format)
				d->audio.convfmt.format = fmt.format;

			if (fmt.sample_rate != in_fmt.sample_rate)
				d->audio.convfmt.sample_rate = fmt.sample_rate;

			if (fmt.channels != in_fmt.channels)
				d->audio.convfmt.channels = fmt.channels;

			return FMED_RMORE;
		}
	}

	if (r != 0) {
		errlog("ffcoraud_open(): (%d) %s", r, ffcoraud_errstr(r));
		return FMED_RERR;
	}

	dbglog("opened buffer %uHz", fmt.sample_rate);
	d->datatype = "pcm";
	return 0;
}

static int coraud_write(void *ctx, fmed_filt *d)
{
	int r;
	struct coraud_out *a = ctx;

	switch (a->state) {
	case 0:
		r = coraud_create(a, d);
		if (r == FMED_RMORE && !d->audio.fmt.ileaved) {
			d->audio.convfmt.ileaved = 1;
			a->state = 1;
			return FMED_RMORE;
		}
		if (r != 0)
			return r;
		if (!d->audio.fmt.ileaved) {
			d->audio.convfmt.ileaved = 1;
			a->state = 1;
			return FMED_RMORE;
		}
		a->state = 2;
		break;

	case 1:
		if (!d->audio.fmt.ileaved)
			return FMED_RERR;
		r = coraud_create(a, d);
		if (r != 0)
			return r;
		a->state = 2;
		break;

	case 2:
		break;
	}

	while (d->datalen != 0) {

		r = ffcoraud_write(&mod->out, d->data, d->datalen);
		if (r < 0) {
			errlog("ffcoraud_write(): (%d) %s", r, ffcoraud_errstr(r));
			return FMED_RERR;

		// } else if (r == 0) {
		// 	return FMED_RASYNC;
		}

		d->datalen -= r;
		dbglog("written %u bytes", r);
	}

	return 0;
}
