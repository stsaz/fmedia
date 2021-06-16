/** MPEG Layer3 decode/encode.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/mp3.h>
#include <FF/audio/mpeg.h>
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
const fmed_filter mpeg_decode_filt;

#include <acodec/mpeg-write.h>

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


static void mpeg_dec_close(void *ctx);

static void* mpeg_dec_open(fmed_filt *d)
{
	mpeg_dec *m;
	if (NULL == (m = ffmem_tcalloc1(mpeg_dec)))
		return NULL;

	ffmpg_init();
	int delay = fmed_getval("mpeg_delay");
	delay = (delay != FMED_NULL) ? delay : 0;
	if (0 != ffmpg_open(&m->mpg, delay, 0)) {
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

const fmed_filter mpeg_decode_filt = {
	mpeg_dec_open, mpeg_dec_process, mpeg_dec_close
};
