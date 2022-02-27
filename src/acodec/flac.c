/** FLAC input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <acodec/alib3-bridge/flac.h>


const fmed_core *core;
const fmed_queue *qu;

extern const fmed_filter fmed_flac_output;
extern const fmed_filter fmed_flac_input;
extern const fmed_filter fmed_flacogg_input;
extern int flac_out_config(fmed_conf_ctx *conf);

struct flac_dec {
	ffflac_dec fl;
	ffpcmex fmt;
};

typedef struct flac_enc {
	ffflac_enc fl;
	uint state;
} flac_enc;

static struct flac_out_conf_t {
	byte level;
	byte md5;
} flac_out_conf;


//FMEDIA MODULE
static const void* flac_iface(const char *name);
static int flac_mod_conf(const char *name, fmed_conf_ctx *conf);
static int flac_sig(uint signo);
static void flac_destroy(void);
static const fmed_mod fmed_flac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&flac_iface, &flac_sig, &flac_destroy, &flac_mod_conf
};

//DECODE
static void* flac_dec_create(fmed_filt *d);
static void flac_dec_free(void *ctx);
static int flac_dec_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_flac_dec = {
	&flac_dec_create, &flac_dec_decode, &flac_dec_free
};

//ENCODE
static void* flac_enc_create(fmed_filt *d);
static void flac_enc_free(void *ctx);
static int flac_enc_encode(void *ctx, fmed_filt *d);
static int flac_enc_config(fmed_conf_ctx *conf);
static const fmed_filter mod_flac_enc = {
	&flac_enc_create, &flac_enc_encode, &flac_enc_free
};

static const fmed_conf_arg flac_enc_conf_args[] = {
	{ "compression",  FMC_INT8,  FMC_O(struct flac_out_conf_t, level) },
	{ "md5",	FMC_BOOL8,  FMC_O(struct flac_out_conf_t, md5) },
	{}
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
	else if (ffsz_eq(name, "out"))
		return &fmed_flac_output;
	else if (ffsz_eq(name, "in"))
		return &fmed_flac_input;
	else if (ffsz_eq(name, "ogg-in"))
		return &fmed_flacogg_input;
	return NULL;
}

static int flac_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return flac_enc_config(ctx);
	else if (ffsz_eq(name, "out"))
		return flac_out_config(ctx);
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


static void* flac_dec_create(fmed_filt *d)
{
	int r;
	struct flac_dec *f = ffmem_new(struct flac_dec);
	if (f == NULL)
		return NULL;

	struct flac_info info;
	info.minblock = fmed_getval("flac.in.minblock");
	info.maxblock = fmed_getval("flac.in.maxblock");
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

	int64 sk = fmed_popval("flac.in.seeksample");
	if (sk != FMED_NULL)
		ffflac_dec_seek(&f->fl, sk);
	uint samples = fmed_getval("flac.in.frsamples");
	uint64 pos = fmed_getval("flac.in.frpos");
	ffstr s;
	ffstr_set(&s, d->data, d->datalen);
	ffflac_dec_input(&f->fl, &s, samples, pos);

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
	if (d->audio.abs_seek != 0)
		d->audio.pos -= fmed_apos_samples(d->audio.abs_seek, f->fmt.sample_rate);

	d->datalen = 0;
	d->outlen = ffflac_dec_output(&f->fl, &d->outni);
	dbglog(core, d->trk, "flac", "decoded %L samples (%U)"
		, d->outlen / ffpcm_size1(&f->fmt), ffflac_dec_cursample(&f->fl));
	return FMED_ROK;
}


static int flac_enc_config(fmed_conf_ctx *conf)
{
	flac_out_conf.level = 6;
	flac_out_conf.md5 = 1;
	fmed_conf_addctx(conf, &flac_out_conf, flac_enc_conf_args);
	return 0;
}

static void* flac_enc_create(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
		return NULL;
	}

	flac_enc *f;
	if (NULL == (f = ffmem_new(flac_enc)))
		return NULL;
	ffflac_enc_init(&f->fl);
	return f;
}

static void flac_enc_free(void *ctx)
{
	flac_enc *f = ctx;
	ffflac_enc_close(&f->fl);
	ffmem_free(f);
}

static int flac_enc_encode(void *ctx, fmed_filt *d)
{
	flac_enc *f = ctx;
	int r;

	switch (f->state) {

	case 0: {
		uint md5 = (d->flac.md5 != -1) ? d->flac.md5 : flac_out_conf.md5;
		if (!md5)
			f->fl.opts |= FFFLAC_ENC_NOMD5;
		f->fl.level = (d->flac.compression != -1) ? d->flac.compression : flac_out_conf.level;
		// break
	}

	case 1:
		if (0 != (r = ffflac_create(&f->fl, (void*)&d->audio.convfmt))) {

			if (f->state == 0 && r == FLAC_EFMT) {
				d->audio.convfmt.ileaved = 0;
				f->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, NULL, "ffflac_create(): %s", ffflac_enc_errstr(&f->fl));
			return FMED_RERR;
		}
		d->track->setvalstr(d->trk, "flac.vendor", flac_vendor());
		d->datatype = "flac";
		// break

	case 2:
		if (d->audio.convfmt.ileaved) {
			if (f->state == 0) {
				d->audio.convfmt.ileaved = 0;
				f->state = 2;
				return FMED_RMORE;
			}
			errlog(core, d->trk, NULL, "unsupported input PCM format");
			return FMED_RERR;
		}
		break;

	case 3:
		break;
	}

	if (d->flags & FMED_FFWD) {
		f->fl.pcm = (const void**)d->datani;
		f->fl.pcmlen = d->datalen;
		if (d->flags & FMED_FLAST)
			ffflac_enc_fin(&f->fl);
	}

	if (f->state != 3) {
		f->state = 3;
		d->out = (void*)&f->fl.info,  d->outlen = sizeof(struct flac_info);
		return FMED_RDATA;
	}

	r = ffflac_encode(&f->fl);

	switch (r) {
	case FFFLAC_RMORE:
		return FMED_RMORE;

	case FFFLAC_RDATA:
		fmed_setval("flac_in_frsamples", f->fl.frsamps);
		break;

	case FFFLAC_RDONE:
		d->out = (void*)&f->fl.info,  d->outlen = sizeof(f->fl.info);
		return FMED_RDONE;

	case FFFLAC_RERR:
	default:
		errlog(core, d->trk, "flac", "ffflac_encode(): %s", ffflac_enc_errstr(&f->fl));
		return FMED_RERR;
	}

	d->out = (void*)f->fl.data;
	d->outlen = f->fl.datalen;
	dbglog(core, d->trk, NULL, "output: %L bytes"
		, d->outlen);
	return FMED_RDATA;
}
