/** Opus input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/opus.h>
#include <FF/data/mmtag.h>


static const fmed_core *core;
static const fmed_queue *qu;

//FMEDIA MODULE
static const void* opus_iface(const char *name);
static int opus_mod_conf(const char *name, ffpars_ctx *ctx);
static int opus_sig(uint signo);
static void opus_destroy(void);
static const fmed_mod fmed_opus_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&opus_iface, &opus_sig, &opus_destroy, &opus_mod_conf
};

//DECODE
static void* opus_open(fmed_filt *d);
static void opus_close(void *ctx);
static int opus_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter opus_input = {
	&opus_open, &opus_in_decode, &opus_close
};

//ENCODE CONFIG
static struct opus_out_conf_t {
	ushort min_tag_size;
	uint bitrate;
	uint frame_size;
	uint complexity;
	uint bandwidth;
} opus_out_conf;

static const ffpars_arg opus_out_conf_args[] = {
	{ "min_tag_size",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(struct opus_out_conf_t, min_tag_size) },
	{ "bitrate",  FFPARS_TINT,  FFPARS_DSTOFF(struct opus_out_conf_t, bitrate) },
	{ "frame_size",  FFPARS_TINT,  FFPARS_DSTOFF(struct opus_out_conf_t, frame_size) },
	{ "complexity",  FFPARS_TINT,  FFPARS_DSTOFF(struct opus_out_conf_t, complexity) },
	{ "bandwidth",  FFPARS_TINT,  FFPARS_DSTOFF(struct opus_out_conf_t, bandwidth) },
};

//ENCODE
static int opus_out_config(ffpars_ctx *ctx);
static void* opus_out_create(fmed_filt *d);
static void opus_out_free(void *ctx);
static int opus_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter opus_output = {
	&opus_out_create, &opus_out_encode, &opus_out_free
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_opus_mod;
}


static const void* opus_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &opus_input;
	else if (!ffsz_cmp(name, "encode"))
		return &opus_output;
	return NULL;
}

static int opus_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return opus_out_config(ctx);
	return -1;
}


static int opus_sig(uint signo)
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

static void opus_destroy(void)
{
}


typedef struct opus_in {
	uint state;
	ffopus opus;
	uint sampsize;
	uint64 pagepos;
	uint stmcopy :1;
} opus_in;

static void* opus_open(fmed_filt *d)
{
	opus_in *o;
	if (NULL == (o = ffmem_tcalloc1(opus_in)))
		return NULL;

	if (0 != ffopus_open(&o->opus)) {
		errlog(core, d->trk, NULL, "ffopus_open(): %s", ffopus_errstr(&o->opus));
		ffmem_free(o);
		return NULL;
	}

	o->stmcopy = (FMED_PNULL != d->track->getvalstr(d->trk, "data_asis"));
	return o;
}

static void opus_close(void *ctx)
{
	opus_in *o = ctx;
	ffopus_close(&o->opus);
	ffmem_free(o);
}

static int opus_in_decode(void *ctx, fmed_filt *d)
{
	enum { R_HDR, R_TAGS, R_DATA1, R_DATA };
	opus_in *o = ctx;
	uint64 pos;

	switch (o->state) {
	case R_HDR:
	case R_TAGS:
		if (!(d->flags & FMED_FFWD))
			return FMED_RMORE;

		o->state++;
		break;

	case R_DATA1:
		if (o->stmcopy) {
			d->meta_block = 0;
			d->out = d->data,  d->outlen = d->datalen;
			return FMED_RDONE;
		}

		if ((int64)d->audio.total != FMED_NULL) {
			o->opus.total_samples = d->audio.total;
			d->audio.total -= o->opus.info.preskip;
		}

		if (d->input_info)
			return FMED_RDONE;

		o->state = R_DATA;
		// break

	case R_DATA:
		if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
			uint64 seek = ffpcm_samples(d->audio.seek, o->opus.info.rate);
			ffopus_seek(&o->opus, seek);
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	int r;
	ffstr in = {0};
	if (d->flags & FMED_FFWD) {
		ffstr_set(&in, d->data, d->datalen);
		d->datalen = 0;

		if (o->pagepos != d->audio.pos) {
			o->opus.pos = d->audio.pos;
			o->pagepos = d->audio.pos;
		}
	}

	for (;;) {

	r = ffopus_decode(&o->opus, in.ptr, in.len);

	switch (r) {

	case FFOPUS_RDATA:
		goto data;

	case FFOPUS_RERR:
		errlog(core, d->trk, NULL, "ffopus_decode(): %s", ffopus_errstr(&o->opus));
		return FMED_RERR;

	case FFOPUS_RWARN:
		warnlog(core, d->trk, NULL, "ffopus_decode(): %s", ffopus_errstr(&o->opus));
		// break

	case FFOPUS_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFOPUS_RHDR:
		d->track->setvalstr(d->trk, "pcm_decoder", "Opus");
		d->audio.fmt.format = FFPCM_FLOAT;
		d->audio.fmt.channels = o->opus.info.channels;
		d->audio.fmt.sample_rate = o->opus.info.rate;
		d->audio.fmt.ileaved = 1;
		o->sampsize = ffpcm_size1(&d->audio.fmt);

		if (o->stmcopy) {
			d->meta_block = 1;
			d->out = in.ptr,  d->outlen = in.len;
			return FMED_RDATA; //HDR packet
		}

		return FMED_RMORE;

	case FFOPUS_RTAG: {
		const ffvorbtag *vtag = &o->opus.vtag;
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

	case FFOPUS_RHDRFIN:
		if (o->stmcopy) {
			d->out = in.ptr,  d->outlen = in.len;
			return FMED_RDATA; //TAGS packet
		}
		return FMED_RMORE;
	}
	}

data:
	pos = ffopus_pos(&o->opus);
	dbglog(core, d->trk, NULL, "decoded %u samples (%U)"
		, o->opus.pcm.len / o->sampsize, pos);
	d->audio.pos = pos - o->opus.pcm.len / ffpcm_size1(&d->audio.fmt);
	d->out = o->opus.pcm.ptr,  d->outlen = o->opus.pcm.len;
	return FMED_RDATA;
}


typedef struct opus_out {
	uint state;
	ffpcm fmt;
	ffopus_enc opus;
	uint64 npkt;
} opus_out;

static int opus_out_config(ffpars_ctx *ctx)
{
	opus_out_conf.min_tag_size = 1000;
	opus_out_conf.bitrate = 192;
	opus_out_conf.frame_size = 40;
	ffpars_setargs(ctx, &opus_out_conf, opus_out_conf_args, FFCNT(opus_out_conf_args));
	return 0;
}

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

	if ((int64)d->audio.total != FMED_NULL) {
		char buf[64];
		uint64 total = d->audio.total * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;
		uint n = ffs_fromint(total, buf, sizeof(buf), 0);
		if (0 != ffopus_addtag(&o->opus, "AUDIO_TOTAL", buf, n))
			warnlog(core, d->trk, NULL, "can't add tag: %s", "AUDIO_TOTAL");
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
		return FMED_RDONE;

	case FFOPUS_RDATA:
		break;

	case FFOPUS_RERR:
		errlog(core, d->trk, NULL, "ffopus_encode(): %s", ffopus_enc_errstr(&o->opus));
		return FMED_RERR;
	}

	o->npkt++;
	if (o->npkt == 1 || o->npkt == 2)
		fmed_setval("ogg_flush", 1);

	fmed_setval("ogg_granpos", ffopus_enc_pos(&o->opus));

	dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes"
		, (d->datalen - o->opus.pcmlen) / ffpcm_size1(&o->fmt), o->opus.data.len);
	d->out = o->opus.data.ptr,  d->outlen = o->opus.data.len;
	return FMED_RDATA;
}
