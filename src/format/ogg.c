/** OGG input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/ogg.h>
#include <FF/array.h>
#include <FF/path.h>
#include <FFOS/random.h>


static const fmed_core *core;

typedef struct fmed_ogg {
	ffogg og;
	uint sample_rate;
	uint state;
	uint seek_ready :1;
	uint seek_done :1;
	uint stmcopy :1;
} fmed_ogg;

typedef struct ogg_out {
	ffogg_cook og;
	uint state;
} ogg_out;

static struct ogg_in_conf_t {
	byte seekable;
} ogg_in_conf;

static struct ogg_out_conf_t {
	ushort max_page_duration;
} ogg_out_conf;


//FMEDIA MODULE
static const void* ogg_iface(const char *name);
static int ogg_conf(const char *name, ffpars_ctx *ctx);
static int ogg_sig(uint signo);
static void ogg_destroy(void);
static const fmed_mod fmed_ogg_mod = {
	&ogg_iface, &ogg_sig, &ogg_destroy, &ogg_conf
};

//DECODE
static void* ogg_open(fmed_filt *d);
static void ogg_close(void *ctx);
static int ogg_decode(void *ctx, fmed_filt *d);
static int ogg_dec_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_ogg_input = {
	&ogg_open, &ogg_decode, &ogg_close
};

static const ffpars_arg ogg_in_conf_args[] = {
	{ "seekable",  FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct ogg_in_conf_t, seekable) }
};

//ENCODE
static void* ogg_out_open(fmed_filt *d);
static void ogg_out_close(void *ctx);
static int ogg_out_encode(void *ctx, fmed_filt *d);
static int ogg_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_ogg_output = {
	&ogg_out_open, &ogg_out_encode, &ogg_out_close
};

static const ffpars_arg ogg_out_conf_args[] = {
	{ "max_page_duration",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(struct ogg_out_conf_t, max_page_duration) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_ogg_mod;
}


static const void* ogg_iface(const char *name)
{
	if (!ffsz_cmp(name, "input")) {
		return &fmed_ogg_input;
	} else if (!ffsz_cmp(name, "output")) {
		return &fmed_ogg_output;
	}
	return NULL;
}

static int ogg_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "input"))
		return ogg_dec_conf(ctx);
	else if (!ffsz_cmp(name, "output"))
		return ogg_out_config(ctx);
	return -1;
}

static int ogg_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT: {
		ffmem_init();
		fftime t;
		fftime_now(&t);
		ffrnd_seed(t.s);
		return 0;
	}

	case FMED_OPEN:
		break;
	}
	return 0;
}

static void ogg_destroy(void)
{
}


static int ogg_dec_conf(ffpars_ctx *ctx)
{
	ogg_in_conf.seekable = 1;
	ffpars_setargs(ctx, &ogg_in_conf, ogg_in_conf_args, FFCNT(ogg_in_conf_args));
	return 0;
}

static void* ogg_open(fmed_filt *d)
{
	fmed_ogg *o = ffmem_tcalloc1(fmed_ogg);
	if (o == NULL)
		return NULL;
	ffogg_init(&o->og);

	if ((int64)d->input.size != FMED_NULL)
		o->og.total_size = d->input.size;

	if (ogg_in_conf.seekable)
		o->og.seekable = 1;

	if (1 == fmed_getval("stream_copy")) {
		d->track->setvalstr(d->trk, "data_asis", "ogg");
		o->stmcopy = 1;
	}
	return o;
}

static void ogg_close(void *ctx)
{
	fmed_ogg *o = ctx;
	ffogg_close(&o->og);
	ffmem_free(o);
}

static const char *const ogg_mods[][2] = {
	{ "vorbis.decode", "opus.decode" },
	{ "vorbis.encode", "opus.encode" },
};

static const char* ogg_codec_mod(const char *fn, uint is_encoder)
{
	ffstr name, ext;
	ffpath_split2(fn, ffsz_len(fn), NULL, &name);
	ffpath_splitname(name.ptr, name.len, NULL, &ext);
	return ogg_mods[is_encoder][ffstr_eqcz(&ext, "opus")];
}

static int ogg_decode(void *ctx, fmed_filt *d)
{
	enum { I_DEC, I_DATA };
	fmed_ogg *o = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	switch (o->state) {
	case I_DEC: {
		const char *dec = ogg_codec_mod(d->track->getvalstr(d->trk, "input"), 0);
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)dec)) {
			return FMED_RERR;
		}
		o->state = I_DATA;
		//break
	}

	case I_DATA:
		break;
	}

	if (d->flags & FMED_FFWD) {
		o->og.data = d->data;
		o->og.datalen = d->datalen;
		d->datalen = 0;
	}

	for (;;) {

		if (o->seek_ready && (int64)d->audio.seek != FMED_NULL && !o->seek_done) {
			o->seek_done = 1;
			ffogg_seek(&o->og, ffpcm_samples(d->audio.seek, o->sample_rate));
			if (o->stmcopy)
				d->audio.seek = FMED_NULL;
		}

		r = ffogg_read(&o->og);
		switch (r) {
		case FFOGG_RMORE:
			if (d->flags & FMED_FLAST) {
				dbglog(core, d->trk, "ogg", "no eos page");
				d->outlen = 0;
				return FMED_RLASTOUT;
			}
			return FMED_RMORE;

		case FFOGG_RHDR:
			// break

		case FFOGG_RDATA:
			goto data;

		case FFOGG_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFOGG_RHDRFIN:
			break;

		case FFOGG_RINFO:
			d->audio.total = o->og.total_samples;
			o->sample_rate = d->audio.fmt.sample_rate;
			o->seek_ready = 1;
			d->audio.bitrate = ffogg_bitrate(&o->og, d->audio.fmt.sample_rate);
			break;

		case FFOGG_RSEEK:
			d->input.seek = o->og.off;
			return FMED_RMORE;

		case FFOGG_RWARN:
			warnlog(core, d->trk, "ogg", "near sample %U, offset %xU: ffogg_read(): %s"
				, ffogg_cursample(&o->og), o->og.off, ffogg_errstr(o->og.err));
			break;

		case FFOGG_RERR:
		default:
			errlog(core, d->trk, "ogg", "ffogg_read(): %s", ffogg_errstr(o->og.err));
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "ogg", "packet #%u, %L bytes, page #%u, granule pos: %U"
		, o->og.pktno, o->og.out.len, ffogg_pageno(&o->og), ffogg_granulepos(&o->og));

	if (o->stmcopy) {
		uint64 set_gpos = (uint64)-1;
		if (ffogg_page_last_pkt(&o->og)) {
			fmed_setval("ogg_flush", 1);
			set_gpos = ffogg_granulepos(&o->og);
		}
		fmed_setval("ogg_granpos", set_gpos);
	}

	if (ffogg_cursample(&o->og) != (uint64)-1)
		d->audio.pos = ffogg_cursample(&o->og);
	o->seek_done = 0;
	d->out = o->og.out.ptr,  d->outlen = o->og.out.len;
	return FMED_RDATA;
}


static int ogg_out_config(ffpars_ctx *ctx)
{
	ogg_out_conf.max_page_duration = 1000;
	ffpars_setargs(ctx, &ogg_out_conf, ogg_out_conf_args, FFCNT(ogg_out_conf_args));
	return 0;
}

static void* ogg_out_open(fmed_filt *d)
{
	ogg_out *o = ffmem_tcalloc1(ogg_out);
	if (o == NULL)
		return NULL;
	return o;
}

static void ogg_out_close(void *ctx)
{
	ogg_out *o = ctx;
	ffogg_wclose(&o->og);
	ffmem_free(o);
}

static int ogg_out_encode(void *ctx, fmed_filt *d)
{
	enum { I_CONF, I_CREAT, I_ENCODE };
	ogg_out *o = ctx;
	int r;

	switch (o->state) {
	case I_CONF: {
		const char *copyfmt;
		if (FMED_PNULL != (copyfmt = d->track->getvalstr(d->trk, "data_asis"))) {
			if (ffsz_cmp(copyfmt, "ogg")) {
				errlog(core, d->trk, NULL, "unsupported input data format: %s", copyfmt);
				return FMED_RERR;
			}

			if (0 != (r = ffogg_create(&o->og, ffrnd_get()))) {
				errlog(core, d->trk, "ogg", "ffogg_create() failed: %s", ffogg_errstr(r));
				return FMED_RERR;
			}
			o->og.allow_partial = 1;

			o->state = I_ENCODE;
			break;
		}

		const char *enc = ogg_codec_mod(d->track->getvalstr(d->trk, "output"), 1);
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)enc)) {
			return FMED_RERR;
		}
		o->state = I_CREAT;
		return FMED_RMORE;
	}

	case I_CREAT:
		if (0 != (r = ffogg_create(&o->og, ffrnd_get()))) {
			errlog(core, d->trk, "ogg", "ffogg_create() failed: %s", ffogg_errstr(r));
			return FMED_RERR;
		}
		o->og.max_pagedelta = ffpcm_samples(ogg_out_conf.max_page_duration, d->audio.convfmt.sample_rate);
		o->state = I_ENCODE;
		//break;

	case I_ENCODE:
		break;
	}

	if (d->flags & FMED_FFWD) {
		o->og.fin = !!(d->flags & FMED_FLAST);
		o->og.flush = (1 == fmed_getval("ogg_flush"));
		o->og.pkt_endpos = fmed_getval("ogg_granpos");
		ffstr_set(&o->og.pkt, d->data, d->datalen);
		d->datalen = 0;
	}

	r = ffogg_write(&o->og);
	switch (r) {

	case FFOGG_RDONE:
		core->log(FMED_LOG_INFO, d->trk, NULL, "OGG: packets:%U, pages:%U, overhead: %.2F%%"
			, o->og.stat.npkts, o->og.stat.npages
			, (double)o->og.stat.total_ogg * 100 / (o->og.stat.total_payload + o->og.stat.total_ogg));
		// break

	case FFOGG_RDATA:
		fmed_setval("ogg_flush", 0);
		goto data;

	case FFOGG_RMORE:
		return FMED_RMORE;

	default:
		errlog(core, d->trk, "ogg", "ffogg_write() failed: %s", ffogg_errstr(o->og.err));
		return FMED_RERR;
	}

data:
	d->out = o->og.out.ptr,  d->outlen = o->og.out.len;

	dbglog(core, d->trk, "ogg", "output: %L bytes, page: %u"
		, (size_t)d->outlen, o->og.page.number - 1);

	return (r == FFOGG_RDONE) ? FMED_RLASTOUT : FMED_RDATA;
}
