/** fmedia: Opus encode
2016, Simon Zolin */

static struct opus_out_conf_t {
	ushort min_tag_size;
	uint bitrate;
	uint frame_size;
	uint complexity;
	uint bandwidth;
} opus_out_conf;

static const fmed_conf_arg opus_out_conf_args[] = {
	{ "min_tag_size",  FMC_INT16,  FMC_O(struct opus_out_conf_t, min_tag_size) },
	{ "bitrate",  FMC_INT32,  FMC_O(struct opus_out_conf_t, bitrate) },
	{ "frame_size",  FMC_INT32,  FMC_O(struct opus_out_conf_t, frame_size) },
	{ "complexity",  FMC_INT32,  FMC_O(struct opus_out_conf_t, complexity) },
	{ "bandwidth",  FMC_INT32,  FMC_O(struct opus_out_conf_t, bandwidth) },
	{}
};

static int opus_out_config(fmed_conf_ctx *ctx)
{
	opus_out_conf.min_tag_size = 1000;
	opus_out_conf.bitrate = 192;
	opus_out_conf.frame_size = 40;
	fmed_conf_addctx(ctx, &opus_out_conf, opus_out_conf_args);
	return 0;
}


typedef struct opus_out {
	uint state;
	ffpcm fmt;
	ffopus_enc opus;
	uint64 npkt;
	uint64 endpos;
} opus_out;

static void* opus_out_create(fmed_filt *d)
{
	opus_out *o = ffmem_tcalloc1(opus_out);
	if (o == NULL)
		return NULL;
	return o;
}

static void opus_out_free(void *ctx)
{
	opus_out *o = ctx;
	ffopus_enc_close(&o->opus);
	ffmem_free(o);
}

static int opus_out_addmeta(opus_out *o, fmed_filt *d)
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
		if (0 != ffopus_addtag(&o->opus, name.ptr, val->ptr, val->len))
			warnlog(core, d->trk, NULL, "can't add tag: %S", &name);
	}

	return 0;
}

static int opus_out_encode(void *ctx, fmed_filt *d)
{
	opus_out *o = ctx;
	int r;
	enum { W_CONV, W_CREATE, W_DATA };

	switch (o->state) {
	case W_CONV:
		o->opus.orig_sample_rate = d->audio.fmt.sample_rate;
		d->audio.convfmt.format = FFPCM_FLOAT;
		d->audio.convfmt.sample_rate = 48000;
		d->audio.convfmt.ileaved = 1;
		o->state = W_CREATE;
		return FMED_RMORE;

	case W_CREATE:
		ffpcm_fmtcopy(&o->fmt, &d->audio.convfmt);
		if (o->fmt.format != FFPCM_FLOAT
			|| o->fmt.sample_rate != 48000
			|| !d->audio.convfmt.ileaved) {
			errlog(core, d->trk, NULL, "input format must be float32 48kHz interleaved");
			return FMED_RERR;
		}
		d->datatype = "Opus";

		int brate = (d->opus.bitrate != -1) ? d->opus.bitrate : (int)opus_out_conf.bitrate;
		o->opus.bandwidth = (d->opus.bandwidth != -1) ? d->opus.bandwidth : (int)opus_out_conf.bandwidth;
		o->opus.complexity = opus_out_conf.complexity;
		o->opus.packet_dur = (d->opus.frame_size != -1) ? d->opus.frame_size : (int)opus_out_conf.frame_size;
		if (0 != (r = ffopus_create(&o->opus, &o->fmt, brate * 1000))) {
			errlog(core, d->trk, NULL, "ffopus_create(): %s", ffopus_enc_errstr(&o->opus));
			return FMED_RERR;
		}

		o->opus.min_tagsize = opus_out_conf.min_tag_size;
		opus_out_addmeta(o, d);

		if ((int64)d->audio.total != FMED_NULL) {
			uint64 total = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);
			d->output.size = ffopus_enc_size(&o->opus, total);
		}

		o->state = W_DATA;
		break;
	}

	if (d->flags & FMED_FLAST)
		o->opus.fin = 1;

	if (d->flags & FMED_FFWD)
		o->opus.pcm = (void*)d->data,  o->opus.pcmlen = d->datalen;

	r = ffopus_encode(&o->opus);

	switch (r) {
	case FFOPUS_RMORE:
		return FMED_RMORE;

	case FFOPUS_RDONE:
		d->outlen = 0;
		d->audio.pos = ffopus_enc_pos(&o->opus);
		return FMED_RDONE;

	case FFOPUS_RDATA:
		break;

	case FFOPUS_RERR:
		errlog(core, d->trk, NULL, "ffopus_encode(): %s", ffopus_enc_errstr(&o->opus));
		return FMED_RERR;
	}

	d->audio.pos = o->endpos;
	o->endpos = ffopus_enc_pos(&o->opus);
	o->npkt++;
	dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes"
		, (d->datalen - o->opus.pcmlen) / ffpcm_size1(&o->fmt), o->opus.data.len);
	d->out = o->opus.data.ptr,  d->outlen = o->opus.data.len;
	return FMED_RDATA;
}

static const fmed_filter opus_output = { opus_out_create, opus_out_encode, opus_out_free };
