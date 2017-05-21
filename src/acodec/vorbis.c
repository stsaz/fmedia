/** Vorbis input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/vorbis.h>
#include <FF/data/mmtag.h>


static const fmed_core *core;
static const fmed_queue *qu;

//FMEDIA MODULE
static const void* vorbis_iface(const char *name);
static int vorbis_conf(const char *name, ffpars_ctx *ctx);
static int vorbis_sig(uint signo);
static void vorbis_destroy(void);
static const fmed_mod fmed_vorbis_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&vorbis_iface, &vorbis_sig, &vorbis_destroy, &vorbis_conf
};

//DECODE
static void* vorbis_open(fmed_filt *d);
static void vorbis_close(void *ctx);
static int vorbis_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter vorbis_input = {
	&vorbis_open, &vorbis_in_decode, &vorbis_close
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
	&vorbis_out_create, &vorbis_out_encode, &vorbis_out_free
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_vorbis_mod;
}


static const void* vorbis_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &vorbis_input;
	else if (!ffsz_cmp(name, "encode"))
		return &vorbis_output;
	return NULL;
}

static int vorbis_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return vorbis_out_config(ctx);
	return -1;
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


typedef struct vorbis_in {
	uint state;
	uint64 pagepos;
	ffvorbis vorbis;
	uint stmcopy :1;
} vorbis_in;

static void* vorbis_open(fmed_filt *d)
{
	vorbis_in *v;
	if (NULL == (v = ffmem_tcalloc1(vorbis_in)))
		return NULL;

	if (0 != ffvorbis_open(&v->vorbis)) {
		errlog(core, d->trk, NULL, "ffvorbis_open(): %s", ffvorbis_errstr(&v->vorbis));
		ffmem_free(v);
		return NULL;
	}

	v->stmcopy = (FMED_PNULL != d->track->getvalstr(d->trk, "data_asis"));

	return v;
}

static void vorbis_close(void *ctx)
{
	vorbis_in *v = ctx;
	ffvorbis_close(&v->vorbis);
	ffmem_free(v);
}

/*
Stream copy:
Pass the first 2 packets with meta_block flag, then close the filter.
*/
static int vorbis_in_decode(void *ctx, fmed_filt *d)
{
	enum { R_HDR, R_TAGS, R_BOOK, R_DATA1, R_DATA };
	vorbis_in *v = ctx;

	switch (v->state) {
	case R_HDR:
	case R_TAGS:
	case R_BOOK:
		if (!(d->flags & FMED_FFWD))
			return FMED_RMORE;

		if (v->state == R_BOOK && v->stmcopy) {
			d->meta_block = 0;
			d->out = d->data,  d->outlen = d->datalen;
			return FMED_RDONE;
		}

		v->state++;
		break;

	case R_DATA1:
		if (d->input_info)
			return FMED_RDONE;

		if ((int64)d->audio.total != FMED_NULL)
			v->vorbis.total_samples = d->audio.total;
		v->state = R_DATA;
		// break

	case R_DATA:
		if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
			uint64 seek = ffpcm_samples(d->audio.seek, ffvorbis_rate(&v->vorbis));
			ffvorbis_seek(&v->vorbis, seek);
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	int r;
	ffstr in = {0};
	if (d->flags & FMED_FFWD) {
		ffstr_set(&in, d->data, d->datalen);
		d->datalen = 0;
		v->vorbis.fin = !!(d->flags & FMED_FLAST);

		if (v->pagepos != d->audio.pos) {
			v->vorbis.cursample = d->audio.pos;
			v->pagepos = d->audio.pos;
		}
	}

	for (;;) {

	r = ffvorbis_decode(&v->vorbis, in.ptr, in.len);

	switch (r) {

	case FFVORBIS_RDATA:
		goto data;

	case FFVORBIS_RERR:
		errlog(core, d->trk, NULL, "ffvorbis_decode(): %s", ffvorbis_errstr(&v->vorbis));
		return FMED_RERR;

	case FFVORBIS_RWARN:
		warnlog(core, d->trk, NULL, "ffvorbis_decode(): %s", ffvorbis_errstr(&v->vorbis));
		// break

	case FFVORBIS_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFVORBIS_RHDR:
		d->track->setvalstr(d->trk, "pcm_decoder", "Vorbis");
		d->audio.fmt.format = FFPCM_FLOAT;
		d->audio.fmt.channels = ffvorbis_channels(&v->vorbis);
		d->audio.fmt.sample_rate = ffvorbis_rate(&v->vorbis);
		d->audio.fmt.ileaved = 0;
		d->audio.bitrate = ffvorbis_bitrate(&v->vorbis);

		if (v->stmcopy) {
			d->meta_block = 1;
			d->out = in.ptr,  d->outlen = in.len;
			return FMED_RDATA; //HDR packet
		}

		return FMED_RMORE;

	case FFVORBIS_RTAG: {
		const ffvorbtag *vtag = &v->vorbis.vtag;
		dbglog(core, d->trk, NULL, "%S: %S", &vtag->name, &vtag->val);
		ffstr name = vtag->name;

		if (ffstr_eqcz(&name, "AUDIO_TOTAL")) {
			uint64 total;
			if (ffstr_toint(&vtag->val, &total, FFS_INT64))
				d->audio.total = total;
			break;
		}

		if (vtag->tag != 0)
			ffstr_setz(&name, ffmmtag_str[vtag->tag]);
		qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, vtag->val.ptr, vtag->val.len, FMED_QUE_TMETA);
		break;
	}

	case FFVORBIS_RHDRFIN:
		if (v->stmcopy) {
			d->out = in.ptr,  d->outlen = in.len;
			return FMED_RDATA; //TAGS packet
		}
		return FMED_RMORE;
	}
	}

data:
	dbglog(core, d->trk, NULL, "decoded %L samples (at %U)"
		, v->vorbis.pcmlen / ffpcm_size(FFPCM_FLOAT, ffvorbis_channels(&v->vorbis)), ffvorbis_cursample(&v->vorbis));
	d->audio.pos = ffvorbis_cursample(&v->vorbis);
	d->out = (void*)v->vorbis.pcm,  d->outlen = v->vorbis.pcmlen;
	return FMED_RDATA;
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

	if ((int64)d->audio.total != FMED_NULL) {
		char buf[64];
		uint64 total = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;
		uint n = ffs_fromint(total, buf, sizeof(buf), 0);
		if (0 != ffvorbis_addtag(&v->vorbis, "AUDIO_TOTAL", buf, n))
			warnlog(core, d->trk, NULL, "can't add tag: %s", "AUDIO_TOTAL");
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
