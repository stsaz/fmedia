/** FLAC input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <acodec/alib3-bridge/flac.h>

const fmed_core *core;
const fmed_queue *qu;

#include <acodec/flac-enc.h>

static const fmed_filter fmed_flac_dec;

//FMEDIA MODULE
static const void* flac_iface(const char *name);
static int flac_mod_conf(const char *name, fmed_conf_ctx *conf);
static int flac_sig(uint signo);
static void flac_destroy(void);
static const fmed_mod fmed_flac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&flac_iface, &flac_sig, &flac_destroy, &flac_mod_conf
};



FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_flac_mod;
}


static const void* flac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_flac_dec;
	else if (!ffsz_cmp(name, "encode"))
		return &mod_flac_enc;
	return NULL;
}

static int flac_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return flac_enc_config(ctx);
	return -1;
}

static int flac_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void flac_destroy(void)
{
}


struct flac_dec {
	ffflac_dec fl;
	ffpcmex fmt;
};

static void flac_dec_free(void *ctx);

static void* flac_dec_create(fmed_filt *d)
{
	int r;
	struct flac_dec *f = ffmem_new(struct flac_dec);
	if (f == NULL)
		return NULL;

	struct flac_info info;
	info.minblock = d->flac_minblock;
	info.maxblock = d->flac_maxblock;
	info.bits = ffpcm_bits(d->audio.fmt.format);
	info.channels = d->audio.fmt.channels;
	info.sample_rate = d->audio.fmt.sample_rate;
	f->fmt = d->audio.fmt;
	if (0 != (r = ffflac_dec_open(&f->fl, &info))) {
		errlog(core, d->trk, "flac", "ffflac_dec_open(): %s", ffflac_dec_errstr(&f->fl));
		flac_dec_free(f);
		return NULL;
	}

	d->datatype = "pcm";
	return f;
}

static void flac_dec_free(void *ctx)
{
	struct flac_dec *f = ctx;
	ffflac_dec_close(&f->fl);
	ffmem_free(f);
}

static int flac_dec_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	struct flac_dec *f = ctx;
	int r;

	if (d->flags & FMED_FLAST) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		ffflac_dec_seek(&f->fl, ffpcm_samples(d->audio.seek, f->fmt.sample_rate));
	}
	ffflac_dec_input(&f->fl, &d->data_in, d->flac_samples, d->audio.pos);
	d->data_in.len = 0;

	r = ffflac_decode(&f->fl);
	switch (r) {
	case FFFLAC_RDATA:
		break;

	case FFFLAC_RWARN:
		warnlog(core, d->trk, "flac", "ffflac_decode(): %s"
			, ffflac_dec_errstr(&f->fl));
		return FMED_RMORE;
	}

	d->audio.pos = ffflac_dec_cursample(&f->fl);
	d->outlen = ffflac_dec_output(&f->fl, &d->outni);
	dbglog(core, d->trk, "flac", "decoded %L samples (%U)"
		, d->outlen / ffpcm_size1(&f->fmt), ffflac_dec_cursample(&f->fl));
	return FMED_ROK;
}

static const fmed_filter fmed_flac_dec = { flac_dec_create, flac_dec_decode, flac_dec_free };
