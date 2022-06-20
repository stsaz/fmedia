/** Opus input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <acodec/alib3-bridge/opus.h>
#include <format/mmtag.h>

#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

static const fmed_core *core;
static const fmed_queue *qu;

#include <acodec/opus-enc.h>

//FMEDIA MODULE
static const void* opus_iface(const char *name);
static int opus_mod_conf(const char *name, fmed_conf_ctx *ctx);
static int opus_sig(uint signo);
static void opus_destroy(void);
static const fmed_mod fmed_opus_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&opus_iface, &opus_sig, &opus_destroy, &opus_mod_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_opus_mod;
}


static const fmed_filter opus_input;
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

static const fmed_filter opus_input = { opus_open, opus_in_decode, opus_close };
