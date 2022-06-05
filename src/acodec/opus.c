/** Opus input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <acodec/alib3-bridge/opus.h>
#include <format/mmtag.h>

#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

static const fmed_core *core;
static const fmed_queue *qu;

//FMEDIA MODULE
static const void* opus_iface(const char *name);
static int opus_mod_conf(const char *name, fmed_conf_ctx *ctx);
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

static const fmed_conf_arg opus_out_conf_args[] = {
	{ "min_tag_size",  FMC_INT16,  FMC_O(struct opus_out_conf_t, min_tag_size) },
	{ "bitrate",  FMC_INT32,  FMC_O(struct opus_out_conf_t, bitrate) },
	{ "frame_size",  FMC_INT32,  FMC_O(struct opus_out_conf_t, frame_size) },
	{ "complexity",  FMC_INT32,  FMC_O(struct opus_out_conf_t, complexity) },
	{ "bandwidth",  FMC_INT32,  FMC_O(struct opus_out_conf_t, bandwidth) },
	{}
};

//ENCODE
static int opus_out_config(fmed_conf_ctx *ctx);
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

static int opus_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return opus_out_config(ctx);
	return -1;
}


static int opus_sig(uint signo)
{
	switch (signo) {
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
	uint last :1;
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

	o->pagepos = (uint64)-1;
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
	int have_tags = 0;
	int reset = 0;
	int r;

	ffstr in = {};
	if (d->flags & FMED_FFWD) {
		ffstr_set(&in, d->data, d->datalen);
		d->datalen = 0;
	}

again:
	switch (o->state) {
	case R_HDR:
	case R_TAGS:
		if (!(d->flags & FMED_FFWD))
			return FMED_RMORE;

		o->state++;
		break;

	case R_DATA1:
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
			reset = 1;
			d->audio.seek = FMED_NULL;
		}
		if (d->flags & FMED_FFWD) {
			if (o->pagepos != d->audio.pos) {
				ffopus_setpos(&o->opus, d->audio.pos, reset);
				o->pagepos = d->audio.pos;
			}
		}
		break;
	}

	if (o->last) {
		return FMED_RDONE;
	}

	ffstr out;
	for (;;) {

	r = ffopus_decode(&o->opus, &in, &out);

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
			if (!o->last) {
				o->last = 1;
				ffopus_flush(&o->opus);
				continue;
			}
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFOPUS_RHDR:
		d->audio.decoder = "Opus";
		d->audio.fmt.format = FFPCM_FLOAT;
		d->audio.fmt.channels = o->opus.info.channels;
		d->audio.fmt.sample_rate = o->opus.info.rate;
		d->audio.fmt.ileaved = 1;
		o->sampsize = ffpcm_size1(&d->audio.fmt);
		d->datatype = "pcm";
		break;

	case FFOPUS_RTAG: {
		have_tags = 1;
		ffstr name, val;
		int tag = ffopus_tag(&o->opus, &name, &val);
		dbglog(core, d->trk, NULL, "%S: %S", &name, &val);
		if (tag != 0)
			ffstr_setz(&name, ffmmtag_str[tag]);
		d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
		break;
	}

	case FFOPUS_RHDRFIN:
		if (!have_tags)
			goto again; // this packet wasn't a Tags packet but audio data
		break;
	}
	}

data:
	d->audio.pos = ffopus_startpos(&o->opus);
	dbglog1(d->trk, "decoded %L samples (at %U)"
		, out.len / o->sampsize, d->audio.pos);
	d->data_out = out;
	return FMED_RDATA;
}


typedef struct opus_out {
	uint state;
	ffpcm fmt;
	ffopus_enc opus;
	uint64 npkt;
	uint64 endpos;
} opus_out;

static int opus_out_config(fmed_conf_ctx *ctx)
{
	opus_out_conf.min_tag_size = 1000;
	opus_out_conf.bitrate = 192;
	opus_out_conf.frame_size = 40;
	fmed_conf_addctx(ctx, &opus_out_conf, opus_out_conf_args);
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
