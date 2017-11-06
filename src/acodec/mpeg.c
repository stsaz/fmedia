/** MPEG Layer3 decode/encode.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/mpeg.h>
#include <FF/audio/mp3lame.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>
#include <FF/array.h>


const fmed_core *core;
const fmed_queue *qu;

typedef struct mpeg_dec {
	uint state;
	ffmpg mpg;
} mpeg_dec;


//FMEDIA MODULE
static const void* mpeg_iface(const char *name);
static int mpeg_mod_conf(const char *name, ffpars_ctx *ctx);
static int mpeg_sig(uint signo);
static void mpeg_destroy(void);
static const fmed_mod fmed_mpeg_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mpeg_iface, &mpeg_sig, &mpeg_destroy, &mpeg_mod_conf
};

extern const fmed_filter fmed_mpeg_input;
extern const fmed_filter fmed_mpeg_output;
extern int mpeg_out_config(ffpars_ctx *ctx);
extern const fmed_filter fmed_mpeg_copy;

//DECODE
static void* mpeg_dec_open(fmed_filt *d);
static void mpeg_dec_close(void *ctx);
static int mpeg_dec_process(void *ctx, fmed_filt *d);
static const fmed_filter mpeg_decode_filt = {
	&mpeg_dec_open, &mpeg_dec_process, &mpeg_dec_close
};

//ENCODE
static void* mpeg_enc_open(fmed_filt *d);
static void mpeg_enc_close(void *ctx);
static int mpeg_enc_process(void *ctx, fmed_filt *d);
static int mpeg_enc_config(ffpars_ctx *ctx);
static const fmed_filter fmed_mpeg_enc = {
	&mpeg_enc_open, &mpeg_enc_process, &mpeg_enc_close
};

typedef struct mpeg_enc {
	uint state;
	ffmpg_enc mpg;
} mpeg_enc;

static struct mpeg_enc_conf_t {
	uint qual;
} mpeg_enc_conf;

static const ffpars_arg mpeg_enc_conf_args[] = {
	{ "quality",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_enc_conf_t, qual) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpeg_mod;
}


static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_mpeg_input;
	else if (!ffsz_cmp(name, "decode"))
		return &mpeg_decode_filt;
	else if (!ffsz_cmp(name, "encode"))
		return &fmed_mpeg_enc;
	else if (!ffsz_cmp(name, "out"))
		return &fmed_mpeg_output;
	else if (!ffsz_cmp(name, "copy"))
		return &fmed_mpeg_copy;
	return NULL;
}

static int mpeg_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return mpeg_enc_config(ctx);
	if (!ffsz_cmp(name, "out"))
		return mpeg_out_config(ctx);
	return -1;
}

static int mpeg_sig(uint signo)
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

static void mpeg_destroy(void)
{
}


static void* mpeg_dec_open(fmed_filt *d)
{
	mpeg_dec *m;
	if (NULL == (m = ffmem_tcalloc1(mpeg_dec)))
		return NULL;

	ffmpg_init();
	if (0 != ffmpg_open(&m->mpg, fmed_getval("mpeg_delay"), 0)) {
		mpeg_dec_close(m);
		return NULL;
	}
	m->mpg.pos = d->audio.pos;

	m->mpg.fmt.sample_rate = d->audio.fmt.sample_rate;
	m->mpg.fmt.channels = d->audio.fmt.channels;
	d->audio.fmt.format = m->mpg.fmt.format;
	d->audio.fmt.ileaved = m->mpg.fmt.ileaved;
	d->audio.decoder = "MPEG";
	d->datatype = "pcm";
	return m;
}

static void mpeg_dec_close(void *ctx)
{
	mpeg_dec *m = ctx;
	ffmpg_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_dec_process(void *ctx, fmed_filt *d)
{
	mpeg_dec *m = ctx;
	int r;

	if (d->flags & FMED_FFWD) {
		ffmpg_input(&m->mpg, d->data, d->datalen);
		d->datalen = 0;
		m->mpg.pos = d->audio.pos;
		m->mpg.fin = (d->flags & FMED_FLAST);
	}

	if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
		uint64 seek = ffpcm_samples(d->audio.seek, m->mpg.fmt.sample_rate);
		ffmpg_seek(&m->mpg, seek);
		d->audio.seek = FMED_NULL;
	}

	for (;;) {
	r = ffmpg_decode(&m->mpg);
	switch (r) {

	case FFMPG_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFMPG_RDATA:
		goto data;

	case FFMPG_RWARN:
		errlog(core, d->trk, "mpeg", "ffmpg_decode(): %s. Near sample %U"
			, ffmpg_errstr(&m->mpg), d->audio.pos);
		continue;
	}
	}

data:
	if (m->mpg.fmt.ileaved)
		d->out = (void*)m->mpg.pcmi;
	// else
	// 	d->outni = (void**)m->mpg.pcm;
	d->outlen = m->mpg.pcmlen;
	dbglog(core, d->trk, "mpeg", "output: %L PCM samples"
		, m->mpg.pcmlen / ffpcm_size1(&m->mpg.fmt));
	d->audio.pos = ffmpg_pos(&m->mpg);
	return FMED_RDATA;
}


static int mpeg_enc_config(ffpars_ctx *ctx)
{
	mpeg_enc_conf.qual = 2;
	ffpars_setargs(ctx, &mpeg_enc_conf, mpeg_enc_conf_args, FFCNT(mpeg_enc_conf_args));
	return 0;
}

static void* mpeg_enc_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
		return NULL;
	}

	mpeg_enc *m = ffmem_new(mpeg_enc);
	if (m == NULL)
		return NULL;

	return m;
}

static void mpeg_enc_close(void *ctx)
{
	mpeg_enc *m = ctx;
	ffmpg_enc_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_enc_process(void *ctx, fmed_filt *d)
{
	mpeg_enc *m = ctx;
	ffpcm pcm;
	int r, qual;

	switch (m->state) {
	case 0:
	case 1:
		ffpcm_fmtcopy(&pcm, &d->audio.convfmt);
		m->mpg.ileaved = d->audio.convfmt.ileaved;

		qual = (d->mpeg.quality != -1) ? d->mpeg.quality : (int)mpeg_enc_conf.qual;
		if (0 != (r = ffmpg_create(&m->mpg, &pcm, qual))) {

			if (r == FFMPG_EFMT && m->state == 0) {
				d->audio.convfmt.format = pcm.format;
				m->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, "mpeg", "ffmpg_create() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}

		if ((int64)d->audio.total != FMED_NULL) {
			uint64 total = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;
			d->output.size = ffmpg_enc_size(&m->mpg, total);
		}
		d->datatype = "mpeg";

		m->state = 2;
		// break

	case 2:
		m->mpg.pcm = (void*)d->data;
		m->mpg.pcmlen = d->datalen;
		m->state = 3;
		// break

	case 3:
		break;
	}

	for (;;) {
		r = ffmpg_encode(&m->mpg);
		switch (r) {

		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (!(d->flags & FMED_FLAST)) {
				m->state = 2;
				return FMED_RMORE;
			}
			m->mpg.fin = 1;
			break;

		case FFMPG_RDONE:
			d->mpg_lametag = 1;
			goto data;

		default:
			errlog(core, d->trk, "mpeg", "ffmpg_encode() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	d->out = m->mpg.data;
	d->outlen = m->mpg.datalen;

	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, m->mpg.datalen);
	return (r == FFMPG_RDONE) ? FMED_RDONE : FMED_RDATA;
}
