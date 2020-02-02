/** JACK input.
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>

#include <FF/adev/jack.h>
#include <FF/array.h>


#undef errlog
#undef dbglog
#define errlog(trk, ...)  fmed_errlog(core, trk, "jack", __VA_ARGS__)
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "jack", __VA_ARGS__)


static const fmed_core *core;

typedef struct jack_mod {
	const fmed_track *track;
	ffbool init_ok;
} jack_mod;
static jack_mod *mod;

//FMEDIA MODULE
static const void* jack_iface(const char *name);
static int jack_conf(const char *name, ffpars_ctx *ctx);
static int jack_sig(uint signo);
static void jack_destroy(void);
static const fmed_mod fmed_jack_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&jack_iface, &jack_sig, &jack_destroy, &jack_conf
};

//ADEV
static int jack_adev_list(fmed_adev_ent **ents, uint flags);
static void jack_adev_listfree(fmed_adev_ent *ents);
static const fmed_adev fmed_jack_adev = {
	.list = &jack_adev_list, .listfree = &jack_adev_listfree,
};

//INPUT
static void* jack_in_open(fmed_filt *d);
static int jack_in_read(void *ctx, fmed_filt *d);
static void jack_in_close(void *ctx);
static const fmed_filter fmed_jack_in = {
	&jack_in_open, &jack_in_read, &jack_in_close
};

static void jack_oncapt(void *udata);

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_jack_mod;
}


static const void* jack_iface(const char *name)
{
	if (ffsz_eq(name, "in"))
		return &fmed_jack_in;
	else if (ffsz_eq(name, "adev"))
		return &fmed_jack_adev;
	return NULL;
}

static int jack_conf(const char *name, ffpars_ctx *ctx)
{
	return -1;
}

static int jack_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		fflk_setup();
		return 0;

	case FMED_OPEN:
		if (NULL == (mod = ffmem_new(jack_mod)))
			return -1;
		mod->track = core->getmod("#core.track");
		return 0;
	}
	return 0;
}

static void jack_destroy(void)
{
	ffjack_uninit();
}


static int jack_initonce()
{
	if (mod->init_ok)
		return 0;

	// A note for the user before using JACK library's functions
	fmed_infolog(core, NULL, "jack", "Note that the messages below may be printed by JACK library directly");

	int r = ffjack_init("fmedia");
	if (r != 0)
		return r;
	mod->init_ok = 1;
	return 0;
}

static int jack_adev_list(fmed_adev_ent **ents, uint flags)
{
	ffarr o = {0};
	ffjack_dev d;
	uint f = 0;
	fmed_adev_ent *e;
	int r, rr = -1;

	if (0 != (r = jack_initonce())) {
		errlog(NULL, "ffjack_init(): %s", ffjack_errstr(r));
		return -1;
	}
	ffjack_devinit(&d);

	if (flags == FMED_ADEV_PLAYBACK)
		f = FFJACK_DEV_PLAYBACK;
	else if (flags == FMED_ADEV_CAPTURE)
		f = FFJACK_DEV_CAPTURE;

	for (;;) {
		r = ffjack_devnext(&d, f);
		if (r == 1)
			break;

		if (NULL == (e = ffarr_pushgrowT(&o, 4, fmed_adev_ent)))
			goto end;
		ffmem_tzero(e);
		if (NULL == (e->name = ffsz_alfmt("%s", ffjack_devname(&d))))
			goto end;
	}

	if (NULL == (e = ffarr_pushT(&o, fmed_adev_ent)))
		goto end;
	e->name = NULL;
	*ents = (void*)o.ptr;
	rr = o.len - 1;

end:
	ffjack_devdestroy(&d);
	if (rr < 0) {
		FFARR_WALKT(&o, e, fmed_adev_ent) {
			ffmem_safefree(e->name);
		}
		ffarr_free(&o);
	}
	return rr;
}

static void jack_adev_listfree(fmed_adev_ent *ents)
{
	fmed_adev_ent *e;
	for (e = ents;  e->name != NULL;  e++) {
		ffmem_free(e->name);
	}
	ffmem_free(ents);
}

/** Get device name by index. */
static const char* jack_finddev(uint idx, uint flags)
{
	int r;
	const char *name = NULL;
	ffjack_dev d;

	ffjack_devinit(&d);

	for (uint i = 1;  ;  i++) {
		r = ffjack_devnext(&d, flags);
		if (r == 1)
			break;
		if (i == idx) {
			name = ffjack_devname(&d);
			goto end;
		}
	}

end:
	ffjack_devdestroy(&d);
	return name;
}


typedef struct jack_in {
    ffjack_buf ja;
	void *trk;
	uint64 total_samples;
	uint frame_size;
} jack_in;

static void* jack_in_open(fmed_filt *d)
{
	int r;
	jack_in *j = ffmem_new(jack_in);
	j->trk = d->trk;
	ffbool first_try = 1;
	int dev_idx;
	ffbool dev_isdefault = 1;
	const char *dev_name = "default";
	ffpcm in_fmt, fmt;

	if (0 != (r = jack_initonce())){
		errlog(d->trk, "ffjack_init(): %s", ffjack_errstr(r));
		goto err;
	}

	// use device specified by user
	if (FMED_NULL != (int)(dev_idx = (int)d->track->getval(d->trk, "capture_device"))) {
		dev_name = jack_finddev(dev_idx, FFJACK_DEV_CAPTURE);
		dev_isdefault = 0;
		if (dev_name == NULL) {
			errlog(d->trk, "no audio device by index #%u", dev_idx);
			goto err;
		}
	}

	ffpcm_fmtcopy(&in_fmt, &d->audio.fmt);

again:
	ffpcm_fmtcopy(&fmt, &d->audio.fmt);
	dbglog(d->trk, "opening device \"%s\", %s/%u/%u"
		, dev_name, ffpcm_fmtstr(fmt.format), fmt.sample_rate, fmt.channels);
	r = ffjack_open(&j->ja, (dev_isdefault) ? NULL : dev_name, &fmt, FFJACK_DEV_CAPTURE);

	if (r == FFJACK_EFMT && first_try && !ffpcm_eq(&fmt, &in_fmt)) {
		first_try = 0;

		if (fmt.format != in_fmt.format) {
			if (d->audio.convfmt.format == 0)
				d->audio.convfmt.format = in_fmt.format;
			d->audio.fmt.format = fmt.format;
		}

		if (fmt.sample_rate != in_fmt.sample_rate) {
			if (d->audio.convfmt.sample_rate == 0)
				d->audio.convfmt.sample_rate = in_fmt.sample_rate;
			d->audio.fmt.sample_rate = fmt.sample_rate;
		}

		if (fmt.channels != in_fmt.channels) {
			if (d->audio.convfmt.channels == 0)
				d->audio.convfmt.channels = in_fmt.channels;
			d->audio.fmt.channels = fmt.channels;
		}

		goto again;
	}

	if (r != 0) {
		errlog(d->trk, "ffjack_open(): %s", ffjack_errstr(r));
		goto err;
	}

	j->ja.handler = &jack_oncapt;
	j->ja.udata = j;
	if (0 != (r = ffjack_start(&j->ja))) {
		errlog(d->trk, "ffjack_start(): %s", ffjack_errstr(r));
		goto err;
	}

	dbglog(d->trk, "started audio capture.  bufsize:%u"
		, (uint)ffjack_bufsize(&j->ja));
	j->frame_size = ffpcm_size1(&fmt);
	d->audio.fmt.ileaved = 1;
	d->datatype = "pcm";
	return j;

err:
	jack_in_close(j);
	return NULL;
}

static void jack_in_close(void *ctx)
{
	jack_in *j = ctx;
	ffjack_async(&j->ja, 0);
	ffjack_close(&j->ja);
	ffmem_free(j);
}

static void jack_oncapt(void *udata)
{
	jack_in *j = udata;
	mod->track->cmd(j->trk, FMED_TRACK_WAKE);
}

static int jack_in_read(void *ctx, fmed_filt *d)
{
	jack_in *j = ctx;
	int r;
	ffstr data;

	if (d->flags & FMED_FSTOP) {
		ffjack_stop(&j->ja);
		d->outlen = 0;
		return FMED_RDONE;
	}

	r = ffjack_read(&j->ja, &data);
	if (r != 0) {
		errlog(d->trk, "ffjack_read(): %s", ffjack_errstr(r));
		return FMED_RERR;
	}
	if (data.len == 0) {
		ffjack_async(&j->ja, 1);
		return FMED_RASYNC;
	}

	dbglog(d->trk, "read %L bytes;  overrun:%d"
		, data.len, ffjack_overrun(&j->ja));

	d->audio.pos = j->total_samples;
	j->total_samples += data.len / j->frame_size;
	d->out = data.ptr,  d->outlen = data.len;
	return FMED_RDATA;
}
