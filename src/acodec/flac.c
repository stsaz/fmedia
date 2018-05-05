/** FLAC input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/flac.h>
#include <FF/audio/flac.h>
#include <FF/mtags/mmtag.h>


const fmed_core *core;
const fmed_queue *qu;

extern const fmed_filter fmed_flac_output;
extern int flac_out_config(ffpars_ctx *conf);

typedef struct flac {
	ffflac fl;
	int64 abs_seek;
	uint state;
} flac;

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
static int flac_mod_conf(const char *name, ffpars_ctx *conf);
static int flac_sig(uint signo);
static void flac_destroy(void);
static const fmed_mod fmed_flac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&flac_iface, &flac_sig, &flac_destroy, &flac_mod_conf
};

//DECODE
static void* flac_in_create(fmed_filt *d);
static void flac_in_free(void *ctx);
static int flac_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_flac_input = {
	&flac_in_create, &flac_in_decode, &flac_in_free
};

static void flac_meta(flac *f, fmed_filt *d);

//ENCODE
static void* flac_enc_create(fmed_filt *d);
static void flac_enc_free(void *ctx);
static int flac_enc_encode(void *ctx, fmed_filt *d);
static int flac_enc_config(ffpars_ctx *conf);
static const fmed_filter mod_flac_enc = {
	&flac_enc_create, &flac_enc_encode, &flac_enc_free
};

static const ffpars_arg flac_enc_conf_args[] = {
	{ "compression",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(struct flac_out_conf_t, level) },
	{ "md5",	FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct flac_out_conf_t, md5) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_flac_mod;
}


static const void* flac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_flac_input;
	else if (!ffsz_cmp(name, "encode"))
		return &mod_flac_enc;
	else if (ffsz_eq(name, "out"))
		return &fmed_flac_output;
	return NULL;
}

static int flac_mod_conf(const char *name, ffpars_ctx *ctx)
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
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void flac_destroy(void)
{
}


static void* flac_in_create(fmed_filt *d)
{
	int r;
	flac *f = ffmem_tcalloc1(flac);
	if (f == NULL)
		return NULL;
	ffflac_init(&f->fl);

	if (FFFLAC_RERR == (r = ffflac_open(&f->fl))) {
		errlog(core, d->trk, "flac", "ffflac_open(): %s", ffflac_errstr(&f->fl));
		flac_in_free(f);
		return NULL;
	}

	if ((int64)d->input.size != FMED_NULL)
		f->fl.total_size = d->input.size;
	return f;
}

static void flac_in_free(void *ctx)
{
	flac *f = ctx;
	ffflac_close(&f->fl);
	ffmem_free(f);
}

static void flac_meta(flac *f, fmed_filt *d)
{
	dbglog(core, d->trk, "flac", "%S: %S", &f->fl.vtag.name, &f->fl.vtag.val);

	ffstr name = f->fl.vtag.name;
	if (f->fl.vtag.tag == FFMMTAG_PICTURE)
		return;
	if (f->fl.vtag.tag != 0) {
		ffstr_setz(&name, ffmmtag_str[f->fl.vtag.tag]);
	}
	d->track->meta_set(d->trk, &name, &f->fl.vtag.val, FMED_QUE_TMETA);
}

static int flac_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	flac *f = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	f->fl.data = d->data;
	f->fl.datalen = d->datalen;
	if (d->flags & FMED_FLAST)
		f->fl.fin = 1;

again:
	switch (f->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			ffflac_seek(&f->fl, f->abs_seek + ffpcm_samples(d->audio.seek, f->fl.fmt.sample_rate));
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	for (;;) {
		r = ffflac_read_decode(&f->fl);
		switch (r) {
		case FFFLAC_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "flac", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFFLAC_RHDR:
			d->audio.decoder = "FLAC";
			ffpcm_fmtcopy(&d->audio.fmt, &f->fl.fmt);
			d->audio.fmt.ileaved = 0;
			d->datatype = "pcm";

			if (d->audio.abs_seek != 0) {
				f->abs_seek = fmed_apos_samples(d->audio.abs_seek, f->fl.fmt.sample_rate);
			}

			d->audio.total = ffflac_totalsamples(&f->fl) - f->abs_seek;
			break;

		case FFFLAC_RTAG:
			flac_meta(f, d);
			break;

		case FFFLAC_RHDRFIN:
			dbglog(core, d->trk, "flac", "blocksize:%u..%u  framesize:%u..%u  MD5:%16xb  seek-table:%u  meta-length:%u"
				, (int)f->fl.info.minblock, (int)f->fl.info.maxblock, (int)f->fl.info.minframe, (int)f->fl.info.maxframe
				, f->fl.info.md5, (int)f->fl.sktab.len, (int)f->fl.framesoff);
			d->audio.bitrate = ffflac_bitrate(&f->fl);

			if (d->input_info)
				return FMED_ROK;

			f->state = I_DATA;
			if (f->abs_seek != 0)
				ffflac_seek(&f->fl, f->abs_seek);
			goto again;

		case FFFLAC_RDATA:
			goto data;

		case FFFLAC_RSEEK:
			d->input.seek = f->fl.off;
			return FMED_RMORE;

		case FFFLAC_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFFLAC_RWARN:
			warnlog(core, d->trk, "flac", "ffflac_decode(): at offset 0x%xU: %s"
				, f->fl.off, ffflac_errstr(&f->fl));
			break;

		case FFFLAC_RERR:
			errlog(core, d->trk, "flac", "ffflac_decode(): %s", ffflac_errstr(&f->fl));
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "flac", "decoded %L samples (%U)"
		, f->fl.pcmlen / ffpcm_size1(&f->fl.fmt), ffflac_cursample(&f->fl));
	d->audio.pos = ffflac_cursample(&f->fl) - f->abs_seek;

	d->data = (void*)f->fl.data;
	d->datalen = f->fl.datalen;
	d->outni = f->fl.pcm;
	d->outlen = f->fl.pcmlen;
	return FMED_RDATA;
}


static int flac_enc_config(ffpars_ctx *conf)
{
	flac_out_conf.level = 6;
	flac_out_conf.md5 = 1;
	ffpars_setargs(conf, &flac_out_conf, flac_enc_conf_args, FFCNT(flac_enc_conf_args));
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
		if (0 != ffflac_create(&f->fl, (void*)&d->audio.convfmt)) {

			if (f->state == 0 && f->fl.errtype == FLAC_EFMT) {
				d->audio.convfmt.ileaved = 0;
				f->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, NULL, "ffflac_create(): %s", ffflac_enc_errstr(&f->fl));
			return FMED_RERR;
		}
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
		d->out = (void*)&f->fl.info,  d->outlen = sizeof(ffflac_info);
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
