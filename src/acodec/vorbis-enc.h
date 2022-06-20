/** fmedia: Vorbis encode
2016, Simon Zolin */

static struct vorbis_out_conf_t {
	ushort min_tag_size;
	float qual;
} vorbis_out_conf;

static const fmed_conf_arg vorbis_out_conf_args[] = {
	{ "min_tag_size",  FMC_INT16,  FMC_O(struct vorbis_out_conf_t, min_tag_size) },
	{ "quality",  FMC_FLOAT32S,  FMC_O(struct vorbis_out_conf_t, qual) },
	{}
};

static int vorbis_out_config(fmed_conf_ctx *ctx)
{
	vorbis_out_conf.min_tag_size = 1000;
	vorbis_out_conf.qual = 5.0;
	fmed_conf_addctx(ctx, &vorbis_out_conf, vorbis_out_conf_args);
	return 0;
}


typedef struct vorbis_out {
	uint state;
	ffpcm fmt;
	ffvorbis_enc vorbis;
	uint64 npkt;
	ffuint64 endpos;
} vorbis_out;

static void* vorbis_out_create(fmed_filt *d)
{
	vorbis_out *v = ffmem_tcalloc1(vorbis_out);
	if (v == NULL)
		return NULL;
	return v;
}

static void vorbis_out_free(void *ctx)
{
	vorbis_out *v = ctx;
	ffvorbis_enc_close(&v->vorbis);
	ffmem_free(v);
}

static int vorbis_out_addmeta(vorbis_out *v, fmed_filt *d)
{
	uint i;
	ffstr name, *val;
	void *qent;

	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| ffstr_eqcz(&name, "vendor"))
			continue;
		if (0 != ffvorbis_addtag(&v->vorbis, name.ptr, val->ptr, val->len))
			warnlog(core, d->trk, NULL, "can't add tag: %S", &name);
	}

	return 0;
}

static int vorbis_out_encode(void *ctx, fmed_filt *d)
{
	vorbis_out *v = ctx;
	int r;
	enum { W_CONV, W_CREATE, W_DATA, W_FIN };

	switch (v->state) {
	case W_CONV:
		d->audio.convfmt.format = FFPCM_FLOAT;
		d->audio.convfmt.ileaved = 0;
		v->state = W_CREATE;
		return FMED_RMORE;

	case W_CREATE:
		ffpcm_fmtcopy(&v->fmt, &d->audio.convfmt);
		if (d->audio.convfmt.format != FFPCM_FLOAT || d->audio.convfmt.ileaved) {
			errlog(core, d->trk, NULL, "input format must be float32 non-interleaved");
			return FMED_RERR;
		}
		d->datatype = "Vorbis";

		int qual = (d->vorbis.quality != -1) ? (d->vorbis.quality - 10) : vorbis_out_conf.qual * 10;
		if (0 != (r = ffvorbis_create(&v->vorbis, &v->fmt, qual))) {
			errlog(core, d->trk, NULL, "ffvorbis_create(): %s", ffvorbis_enc_errstr(&v->vorbis));
			return FMED_RERR;
		}

		v->vorbis.min_tagsize = vorbis_out_conf.min_tag_size;
		vorbis_out_addmeta(v, d);

		if ((int64)d->audio.total != FMED_NULL) {
			uint64 total = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);
			d->output.size = ffvorbis_enc_size(&v->vorbis, total);
		}

		v->state = W_DATA;
		break;

	case W_FIN:
		d->audio.pos = v->endpos;
		return FMED_RDONE;
	}

	if (d->flags & FMED_FLAST)
		v->vorbis.fin = 1;

	v->vorbis.pcm = (const float**)d->data,  v->vorbis.pcmlen = d->datalen;
	r = ffvorbis_encode(&v->vorbis);

	switch (r) {
	case FFVORBIS_RMORE:
		return FMED_RMORE;

	case FFVORBIS_RDONE:
		v->state = W_FIN;
		break;
	case FFVORBIS_RDATA:
		break;

	case FFVORBIS_RERR:
		errlog(core, d->trk, NULL, "ffvorbis_encode(): %s", ffvorbis_enc_errstr(&v->vorbis));
		return FMED_RERR;
	}

	v->npkt++;
	d->audio.pos = v->endpos;
	v->endpos = ffvorbis_enc_pos(&v->vorbis);
	dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes @%U [%U]"
		, (d->datalen - v->vorbis.pcmlen) / ffpcm_size1(&v->fmt), v->vorbis.data.len
		, d->audio.pos, v->endpos);
	d->data = (void*)v->vorbis.pcm,  d->datalen = v->vorbis.pcmlen;
	d->out = v->vorbis.data.ptr,  d->outlen = v->vorbis.data.len;
	return FMED_RDATA;
}

static const fmed_filter vorbis_output = { vorbis_out_create, vorbis_out_encode, vorbis_out_free };
