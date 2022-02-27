/** Musepack input.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>
#include <acodec/alib3-bridge/musepack.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* mpc_iface(const char *name);
static int mpc_mod_conf(const char *name, fmed_conf_ctx *ctx);
static int mpc_sig(uint signo);
static void mpc_destroy(void);
static const fmed_mod fmed_mpc_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mpc_iface, &mpc_sig, &mpc_destroy, &mpc_mod_conf
};

//DECODE
static void* mpc_dec_open(fmed_filt *d);
static void mpc_dec_close(void *ctx);
static int mpc_dec_process(void *ctx, fmed_filt *d);
static const fmed_filter mpc_decoder = {
	&mpc_dec_open, &mpc_dec_process, &mpc_dec_close
};

typedef struct mpcdec {
	ffmpc mpcdec;
	ffpcm fmt;
} mpcdec;


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpc_mod;
}


static const void* mpc_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &mpc_decoder;
	return NULL;
}

static int mpc_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	return -1;
}

static int mpc_sig(uint signo)
{
	return 0;
}

static void mpc_destroy(void)
{
}


static void* mpc_dec_open(fmed_filt *d)
{
	mpcdec *m;
	if (NULL == (m = ffmem_new(mpcdec)))
		return NULL;
	if (0 != ffmpc_open(&m->mpcdec, &d->audio.fmt, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffmpc_open()");
		ffmem_free(m);
		return NULL;
	}
	ffpcm_fmtcopy(&m->fmt, &d->audio.fmt);
	d->datalen = 0;
	d->datatype = "pcm";
	return m;
}

static void mpc_dec_close(void *ctx)
{
	mpcdec *m = ctx;
	ffmpc_close(&m->mpcdec);
	ffmem_free(m);
}

static int mpc_dec_process(void *ctx, fmed_filt *d)
{
	mpcdec *m = ctx;
	int r;
	ffstr s;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if ((d->flags & FMED_FFWD) && d->datalen != 0) {
		ffmpc_inputblock(&m->mpcdec, d->data, d->datalen, d->audio.pos);
		d->datalen = 0;
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		if (d->flags & FMED_FFWD) {
			uint64 seek = ffpcm_samples(d->audio.seek, m->fmt.sample_rate);
			ffmpc_seek(&m->mpcdec, seek);
			d->audio.seek = FMED_NULL;
		} else {
			m->mpcdec.need_data = 1;
			return FMED_RMORE;
		}
	}

	r = ffmpc_decode(&m->mpcdec);

	switch (r) {
	case FFMPC_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFMPC_RDATA:
		break;

	case FFMPC_RERR:
		warnlog(core, d->trk, "mpc", "ffmpc_decode(): %s", ffmpc_errstr(&m->mpcdec));
		d->codec_err = 1;
		return FMED_RMORE;
	}

	ffmpc_audiodata(&m->mpcdec, &s);
	dbglog(core, d->trk, NULL, "decoded %L samples"
		, s.len / ffpcm_size1(&m->fmt));
	d->out = s.ptr,  d->outlen = s.len;
	d->audio.pos = ffmpc_cursample(&m->mpcdec);
	return FMED_RDATA;
}
