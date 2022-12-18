/** fmedia: AAC encode
2016, Simon Zolin */

#include <acodec/alib3-bridge/aac.h>

static struct aac_out_conf_t {
	uint aot;
	uint qual;
	uint afterburner;
	uint bandwidth;
} aac_out_conf;

static int aac_conf_aot(fmed_conf *fc, void *obj, const ffstr *val);

static const fmed_conf_arg aac_out_conf_args[] = {
	{ "profile",	FMC_STR,  FMC_F(aac_conf_aot) },
	{ "quality",	FMC_INT32,  FMC_O(struct aac_out_conf_t, qual) },
	{ "afterburner",	FMC_INT32,  FMC_O(struct aac_out_conf_t, afterburner) },
	{ "bandwidth",	FMC_INT32,  FMC_O(struct aac_out_conf_t, bandwidth) },
	{}
};

typedef struct aac_out {
	uint state;
	ffpcm fmt;
	ffstr in;
	ffaac_enc aac;
} aac_out;

static int aac_profile(const ffstr *name)
{
	static const char* const aac_profile_str[] = { "he", "hev2", "lc", };
	static const byte aac_profile_val[] = { AAC_HE, AAC_HEV2, AAC_LC, };
	int r = ffszarr_ifindsorted(aac_profile_str, FFCNT(aac_profile_str), name->ptr, name->len);
	if (r < 0)
		return 0;
	return aac_profile_val[r];
}

static int aac_conf_aot(fmed_conf *fc, void *obj, const ffstr *val)
{
	if (0 == (aac_out_conf.aot = aac_profile(val)))
		return FMC_EBADVAL;
	return 0;
}

int aac_out_config(fmed_conf_ctx *ctx)
{
	aac_out_conf.aot = AAC_LC;
	aac_out_conf.qual = 256;
	aac_out_conf.afterburner = 1;
	aac_out_conf.bandwidth = 0;
	fmed_conf_addctx(ctx, &aac_out_conf, aac_out_conf_args);
	return 0;
}

static void* aac_out_create(fmed_track_info *d)
{
	aac_out *a = ffmem_new(aac_out);
	if (a == NULL)
		return NULL;
	return a;
}

static void aac_out_free(void *ctx)
{
	aac_out *a = ctx;
	ffaac_enc_close(&a->aac);
	ffmem_free(a);
}

static int aac_out_encode(void *ctx, fmed_track_info *d)
{
	aac_out *a = ctx;
	int r;
	enum { W_CONV, W_CREATE, W_DATA };

	if (d->flags & FMED_FFWD) {
		a->in = d->data_in;
		d->data_in.len = 0;
	}

	switch (a->state) {
	case W_CONV:
		d->audio.convfmt.format = FFPCM_16;
		d->audio.convfmt.ileaved = 1;
		a->state = W_CREATE;
		return FMED_RMORE;

	case W_CREATE:
		if (d->audio.convfmt.format != FFPCM_16LE || !d->audio.convfmt.ileaved) {
			errlog1(d->trk, "unsupported input PCM format");
			return FMED_RERR;
		}

		ffpcm_fmtcopy(&a->fmt, &d->audio.convfmt);
		d->datatype = "AAC";

		int qual = (d->aac.quality != -1) ? d->aac.quality : (int)aac_out_conf.qual;
		if (qual > 5 && qual < 8000)
			qual *= 1000;

		a->aac.info.aot = aac_out_conf.aot;
		if (d->aac.profile.len != 0) {
			if (0 == (a->aac.info.aot = aac_profile(&d->aac.profile))) {
				errlog1(d->trk, "invalid profile %S", &d->aac.profile);
				return FMED_RERR;
			}
		}
		a->aac.info.afterburner = aac_out_conf.afterburner;
		a->aac.info.bandwidth = (d->aac.bandwidth != -1) ? d->aac.bandwidth : (int)aac_out_conf.bandwidth;

		if (0 != (r = ffaac_create(&a->aac, &a->fmt, qual))) {
			errlog1(d->trk, "ffaac_create(): %s", ffaac_enc_errstr(&a->aac));
			return FMED_RERR;
		}

		d->a_enc_delay = a->aac.info.enc_delay;
		d->a_frame_samples = ffaac_enc_frame_samples(&a->aac);
		d->a_enc_bitrate = ffaac_bitrate(&a->aac, a->aac.info.quality);
		ffstr asc = ffaac_enc_conf(&a->aac);
		dbglog1(d->trk, "using bitrate %ubps, bandwidth %uHz, asc %*xb"
			, ffaac_bitrate(&a->aac, a->aac.info.quality), a->aac.info.bandwidth, asc.len, asc.ptr);

		d->out = asc.ptr,  d->outlen = asc.len;
		a->state = W_DATA;
		return FMED_RDATA;
	}

	if (d->flags & FMED_FLAST)
		a->aac.fin = 1;

	a->aac.pcm = (void*)a->in.ptr,  a->aac.pcmlen = a->in.len;
	r = ffaac_encode(&a->aac);

	switch (r) {
	case FFAAC_RDONE:
		d->outlen = 0;
		return FMED_RDONE;

	case FFAAC_RMORE:
		return FMED_RMORE;

	case FFAAC_RDATA:
		break;

	case FFAAC_RERR:
		errlog1(d->trk, "ffaac_encode(): %s", ffaac_enc_errstr(&a->aac));
		return FMED_RERR;
	}

	dbglog1(d->trk, "encoded %L samples into %L bytes"
		, (a->in.len - a->aac.pcmlen) / ffpcm_size1(&a->fmt), a->aac.datalen);
	ffstr_set(&a->in, (void*)a->aac.pcm, a->aac.pcmlen);
	ffstr_set(&d->data_out, a->aac.data, a->aac.datalen);
	return FMED_RDATA;
}

const fmed_filter aac_output = { aac_out_create, aac_out_encode, aac_out_free };
