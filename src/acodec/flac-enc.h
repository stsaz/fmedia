/** fmedia: FLAC encode
2015, Simon Zolin */

#include <acodec/alib3-bridge/flac.h>

static struct flac_out_conf_t {
	byte level;
	byte md5;
} flac_out_conf;

static const fmed_conf_arg flac_enc_conf_args[] = {
	{ "compression",  FMC_INT8,  FMC_O(struct flac_out_conf_t, level) },
	{ "md5",	FMC_BOOL8,  FMC_O(struct flac_out_conf_t, md5) },
	{}
};

static int flac_enc_config(fmed_conf_ctx *conf)
{
	flac_out_conf.level = 6;
	flac_out_conf.md5 = 1;
	fmed_conf_addctx(conf, &flac_out_conf, flac_enc_conf_args);
	return 0;
}


typedef struct flac_enc {
	ffflac_enc fl;
	uint state;
} flac_enc;

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
		d->flac_vendor = flac_vendor();
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

static const fmed_filter mod_flac_enc = { flac_enc_create, flac_enc_encode, flac_enc_free };
