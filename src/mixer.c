/** Mixer input/output.
Copyright (c) 2015 Simon Zolin */

/*
INPUT1 -> mixer-in \
                    -> mixer-out -> OUTPUT
INPUT2 -> mixer-in /
*/

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/data/parse.h>
#include <FF/array.h>
#include <FF/list.h>
#include <FFOS/error.h>


typedef struct mxr {
	ffstr data;
	fflist inputs; //mix_in[]
	uint trk_count;
	uint filled;
	uint sampsize;
	fftask task;
	unsigned first :1
		, err :1;
} mxr;

typedef struct mix_in {
	fflist_item sib;
	uint off;
	uint state;
	fmed_handler handler;
	void *udata;
	mxr *m;
	unsigned more :1
		, filled :1;
} mix_in;

static struct mix_conf_t {
	ffpcm pcm;
	uint buf_size;
} conf;
#define pcmfmt  (conf.pcm)
#define DATA_SIZE  (conf.buf_size)

static mxr *mx;
static const fmed_core *core;

//FMEDIA MODULE
static const void* mix_iface(const char *name);
static int mix_sig(uint signo);
static void mix_destroy(void);
static const fmed_mod fmed_mix_mod = {
	&mix_iface, &mix_sig, &mix_destroy
};

//INPUT
static void* mix_in_open(fmed_filt *d);
static int mix_in_write(void *ctx, fmed_filt *d);
static void mix_in_close(void *ctx);
static const fmed_filter fmed_mix_in = {
	&mix_in_open, &mix_in_write, &mix_in_close
};

//OUTPUT
static void* mix_open(fmed_filt *d);
static int mix_read(void *ctx, fmed_filt *d);
static void mix_close(void *ctx);
static int mix_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_mix_out = {
	&mix_open, &mix_read, &mix_close, &mix_conf
};

static uint mix_write(mxr *m, uint off, const fmed_filt *d);
static void mix_inputclosed(mxr *m);


static int mix_conf_close(ffparser_schem *p, void *obj);

static int mix_conf_format(ffparser_schem *p, void *obj, ffstr *val)
{
	if (ffstr_eqcz(val, "16le"))
		pcmfmt.format = FFPCM_16LE;
	else if (ffstr_eqcz(val, "32le"))
		pcmfmt.format = FFPCM_32LE;
	else if (ffstr_eqcz(val, "float"))
		pcmfmt.format = FFPCM_FLOAT;
	else
		return FFPARS_EBADVAL;
	return 0;
}

static const ffpars_arg mix_conf_args[] = {
	{ "format",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&mix_conf_format) }
	, { "channels",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(ffpcm, channels) }
	, { "rate",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(ffpcm, sample_rate) }
	, { "buffer",	FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(struct mix_conf_t, buf_size) },
	{ NULL,	FFPARS_TCLOSE, FFPARS_DST(&mix_conf_close) },
};

static int mix_conf_close(ffparser_schem *p, void *obj)
{
	conf.pcm.format = FFPCM_16LE;
	conf.pcm.channels = 2;
	conf.pcm.sample_rate = 44100;
	conf.buf_size = ffpcm_bytes(&conf.pcm, conf.buf_size);
	return 0;
}

static int mix_conf(ffpars_ctx *ctx)
{
	conf.buf_size = 1000;
	ffpars_setargs(ctx, &conf, mix_conf_args, FFCNT(mix_conf_args));
	return 0;
}


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_mix_mod;
}


static const void* mix_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_mix_in;
	else if (!ffsz_cmp(name, "out"))
		return &fmed_mix_out;
	return NULL;
}

static int mix_sig(uint signo)
{
	return 0;
}

static void mix_destroy(void)
{
}


static void* mix_in_open(fmed_filt *d)
{
	mix_in *mi;

	if (mx->err)
		return NULL;

	mi = ffmem_tcalloc1(mix_in);
	if (mi == NULL) {
		errlog(core, d->trk, "mixer", "%e", FFERR_BUFALOC);
		mx->err = 1;
		return NULL;
	}
	fflist_ins(&mx->inputs, &mi->sib);
	mi->m = mx;
	mi->handler = d->handler;
	mi->udata = d->trk;
	return mi;
}

static void mix_in_close(void *ctx)
{
	mix_in *mi = ctx;
	fflist_rm(&mi->m->inputs, &mi->sib);
	FF_ASSERT(mi->m->trk_count != 0);
	mi->m->trk_count--;
	if (mi->filled)
		mi->m->filled--;
	mix_inputclosed(mi->m);
	ffmem_free(mi);
}

static int mix_in_write(void *ctx, fmed_filt *d)
{
	uint n;
	mix_in *mi = ctx;

	if (mi->m->err)
		return FMED_RERR;

	switch (mi->state) {
	case 0:
		d->track->setval(d->trk, "conv_pcm_format", pcmfmt.format);
		d->track->setval(d->trk, "conv_pcm_ileaved", 1);
		mi->state = 1;
		return FMED_RMORE;

	case 1:
		if (pcmfmt.format != d->track->getval(d->trk, "pcm_format")
			|| pcmfmt.channels != d->track->getval(d->trk, "pcm_channels")
			|| pcmfmt.sample_rate != d->track->getval(d->trk, "pcm_sample_rate")) {
			errlog(core, d->trk, "mixer", "input format doesn't match output");
			mx->err = 1;
			return NULL;
		}
		mi->state = 2;
		break;
	}

	n = mix_write(mi->m, mi->off, d);
	mi->off += n;
	d->data += n;
	d->datalen -= n;

	if (mi->off == DATA_SIZE) {
		mi->filled = 1;
		mi->more = 1;
		return FMED_RASYNC; //wait until there's more space in output buffer

	} else if (d->flags & FMED_FLAST) {
		mi->filled = 1;
		return FMED_RDONE;
	}
	return FMED_ROK;
}


static void* mix_open(fmed_filt *d)
{
	mxr *m = ffmem_tcalloc1(mxr);
	if (m == NULL) {
		errlog(core, d->trk, "mixer", "%e", FFERR_BUFALOC);
		return NULL;
	}

	if (NULL == ffstr_alloc(&m->data, DATA_SIZE)) {
		errlog(core, d->trk, "mixer", "%e", FFERR_BUFALOC);
		ffmem_free(m);
		m = NULL;
		return NULL;
	}
	ffmem_zero(m->data.ptr, DATA_SIZE);

	m->task.handler = d->handler;
	m->task.param = d->trk;
	fflist_init(&m->inputs);
	m->first = 1;
	m->sampsize = ffpcm_size(pcmfmt.format, pcmfmt.channels);

	d->track->setval(d->trk, "pcm_format", pcmfmt.format);
	d->track->setval(d->trk, "pcm_channels", pcmfmt.channels);
	d->track->setval(d->trk, "pcm_sample_rate", pcmfmt.sample_rate);
	d->track->setval(d->trk, "pcm_ileaved", 1);

	m->trk_count = fmed_getval("mix_tracks");

	mx = m;
	return m;
}

static void mix_close(void *ctx)
{
	mxr *m = ctx;
	if (m->inputs.len != 0) {
		mix_in *mi;
		fflist_item *next;

		m->err = 1; //stop all input tracks

		FFLIST_WALKSAFE(&m->inputs, mi, sib, next) {
			if (mi->more) {
				mi->more = 0;
				mi->handler(mi->udata);
			}
		}
		return;
	}
	core->task(&m->task, FMED_TASK_DEL);
	ffstr_free(&m->data);
	ffmem_free(m);
	m = NULL;
}

static void mix_inputclosed(mxr *m)
{
	if (m->filled == m->trk_count) {
		if (m->err && m->inputs.len == 0)
			mix_close(m);
		else
			core->task(&m->task, FMED_TASK_POST);
	}
}

static uint mix_write(mxr *m, uint off, const fmed_filt *d)
{
	uint n = (uint)ffmin(DATA_SIZE - off, d->datalen);
	ffpcm_mix(&pcmfmt, m->data.ptr + off, d->data, n / m->sampsize);

	off += n;
	if (off > m->data.len)
		m->data.len = off;

	if (off == DATA_SIZE || (d->flags & FMED_FLAST)) {
		//no more space in output buffer
		//or it's the last chunk of input data

		m->filled++;
		if (m->filled == m->trk_count)
			core->task(&m->task, FMED_TASK_POST);
	}

	return n;
}

static int mix_read(void *ctx, fmed_filt *d)
{
	mix_in *mi;
	fflist_item *next;
	mxr *m = ctx;

	if (m->first) {
		m->first = 0;
		return FMED_RASYNC;
	}

	if (m->filled != m->inputs.len)
		return FMED_RASYNC; //mixed data is not ready

	if (d->outlen != m->data.len) {
		d->out = m->data.ptr;
		d->outlen = m->data.len;
		return FMED_ROK;
	}

	ffmem_zero(m->data.ptr, DATA_SIZE);
	d->outlen = 0;
	m->data.len = 0;
	m->filled = 0;
	FFLIST_WALK(&m->inputs, mi, sib) {
		mi->off = 0;
		mi->filled = 0;
	}

	if (m->inputs.len == 0)
		return FMED_RDONE;

	//notify those streams that have more output
	FFLIST_WALKSAFE(&m->inputs, mi, sib, next) {
		if (mi->more) {
			mi->more = 0;
			mi->handler(mi->udata);
		}
	}

	return FMED_RASYNC;
}
