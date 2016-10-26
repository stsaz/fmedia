/** Vorbis input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/vorbis.h>


static const fmed_core *core;
static const fmed_queue *qu;

//FMEDIA MODULE
static const void* vorbis_iface(const char *name);
static int vorbis_sig(uint signo);
static void vorbis_destroy(void);
static const fmed_mod fmed_vorbis_mod = {
	&vorbis_iface, &vorbis_sig, &vorbis_destroy
};

//ENCODE CONFIG
static struct vorbis_out_conf_t {
	ushort min_tag_size;
	float qual;
} vorbis_out_conf;

static const ffpars_arg vorbis_out_conf_args[] = {
	{ "min_tag_size",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(struct vorbis_out_conf_t, min_tag_size) },
	{ "quality",  FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DSTOFF(struct vorbis_out_conf_t, qual) }
};

//ENCODE
static int vorbis_out_config(ffpars_ctx *ctx);
static void* vorbis_out_create(fmed_filt *d);
static void vorbis_out_free(void *ctx);
static int vorbis_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter vorbis_output = {
	&vorbis_out_create, &vorbis_out_encode, &vorbis_out_free, &vorbis_out_config
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_vorbis_mod;
}


static const void* vorbis_iface(const char *name)
{
	if (!ffsz_cmp(name, "encode"))
		return &vorbis_output;
	return NULL;
}

static int vorbis_sig(uint signo)
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

static void vorbis_destroy(void)
{
}


typedef struct vorbis_out {
	uint state;
	ffpcm fmt;
	ffvorbis_enc vorbis;
	uint64 npkt;
} vorbis_out;

static int vorbis_out_config(ffpars_ctx *ctx)
{
	vorbis_out_conf.min_tag_size = 1000;
	vorbis_out_conf.qual = 5.0;
	ffpars_setargs(ctx, &vorbis_out_conf, vorbis_out_conf_args, FFCNT(vorbis_out_conf_args));
	return 0;
}

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
	enum { W_CONV, W_CREATE, W_DATA };

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

		int qual = (int)fmed_getval("vorbis.quality");
		if (qual == FMED_NULL)
			qual = vorbis_out_conf.qual * 10;

		if (0 != (r = ffvorbis_create(&v->vorbis, &v->fmt, qual))) {
			errlog(core, d->trk, NULL, "ffvorbis_create(): %s", ffvorbis_enc_errstr(&v->vorbis));
			return FMED_RERR;
		}

		v->vorbis.min_tagsize = vorbis_out_conf.min_tag_size;
		vorbis_out_addmeta(v, d);

		if ((int64)d->audio.total != FMED_NULL)
			d->output.size = (d->audio.total / d->audio.convfmt.sample_rate) / ffvorbis_enc_bitrate(&v->vorbis, qual);

		v->state = W_DATA;
		break;
	}

	if (d->flags & FMED_FLAST)
		v->vorbis.fin = 1;

	v->vorbis.pcm = (const float**)d->data,  v->vorbis.pcmlen = d->datalen;
	r = ffvorbis_encode(&v->vorbis);

	switch (r) {
	case FFVORBIS_RMORE:
		return FMED_RMORE;

	case FFVORBIS_RDONE:
	case FFVORBIS_RDATA:
		break;

	case FFVORBIS_RERR:
		errlog(core, d->trk, NULL, "ffvorbis_encode(): %s", ffvorbis_enc_errstr(&v->vorbis));
		return FMED_RERR;
	}

	v->npkt++;
	if (v->npkt == 1 || v->npkt == 3)
		fmed_setval("ogg_flush", 1);

	fmed_setval("ogg_granpos", ffvorbis_enc_pos(&v->vorbis));

	dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes"
		, (d->datalen - v->vorbis.pcmlen) / ffpcm_size1(&v->fmt), v->vorbis.data.len);
	d->data = (void*)v->vorbis.pcm,  d->datalen = v->vorbis.pcmlen;
	d->out = v->vorbis.data.ptr,  d->outlen = v->vorbis.data.len;
	return (r == FFVORBIS_RDONE) ? FMED_RDONE : FMED_RDATA;
}
