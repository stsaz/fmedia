/** Mixer input/output.
Copyright (c) 2015 Simon Zolin */

/*
INPUT1 -> mixer-in \
                    -> mixer-out -> OUTPUT
INPUT2 -> mixer-in /
*/

#include <fmedia.h>
#include <afilter/pcm.h>
#include <util/array.h>
#include <FFOS/error.h>

#undef dbglog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "mixer", __VA_ARGS__)

typedef struct mxr {
	ffstr data;
	fflist inputs; //mix_in[]
	uint trk_count;
	uint filled;
	uint sampsize;
	void *trk;
	unsigned first :1
		, clear :1
		, err :1;
} mxr;

typedef struct mix_in {
	fflist_item sib;
	uint off;
	uint state;
	void *trk;
	mxr *m;
	unsigned more :1
		, filled :1;
} mix_in;

static struct mix_conf_t {
	ffpcmex pcm;
	uint buf_size;
} conf;
#define pcmfmt  (conf.pcm)
#define DATA_SIZE  (conf.buf_size)

static mxr *mx;
extern const fmed_core *core;
extern const fmed_track *track;

static uint mix_write(mxr *m, uint off, const fmed_filt *d);
static ffbool mix_input_opened(mxr *m, mix_in *mi);
static void mix_input_closed(mxr *m, mix_in *mi);
#define mix_err(m)  ((m) == NULL || (m)->err)
static void mix_seterr(mxr *m);


static int mix_conf_close(fmed_conf *fc, void *obj);

static int mix_conf_format(fmed_conf *fc, void *obj, ffstr *val)
{
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FMC_EBADVAL;
	pcmfmt.format = r;
	return 0;
}

static const fmed_conf_arg mix_conf_args[] = {
	{ "format",  FMC_STRNE, FMC_F(mix_conf_format) }
	, { "channels",  FMC_INT32NZ, FMC_O(ffpcm, channels) }
	, { "rate",  FMC_INT32NZ, FMC_O(ffpcm, sample_rate) }
	, { "buffer",	FMC_INT32NZ, FMC_O(struct mix_conf_t, buf_size) },
	{ NULL,	FMC_ONCLOSE, FMC_F(mix_conf_close) },
};

static int mix_conf_close(fmed_conf *fc, void *obj)
{
	conf.buf_size = ffpcm_bytes(&conf.pcm, conf.buf_size);
	return 0;
}

int mix_out_conf(fmed_conf_ctx *ctx)
{
	conf.pcm.format = FFPCM_16;
	conf.pcm.channels = 2;
	conf.pcm.sample_rate = 44100;
	conf.buf_size = 1000;
	fmed_conf_addctx(ctx, &conf, mix_conf_args);
	return 0;
}


static void* mix_in_open(fmed_filt *d)
{
	if (mx == NULL)
		return NULL;
	mix_in *mi;

	mi = ffmem_tcalloc1(mix_in);
	if (mi == NULL) {
		errlog(core, d->trk, "mixer", "%s", ffmem_alloc_S);
		mix_seterr(mx);
		return NULL;
	}
	if (!mix_input_opened(mx, mi)) {
		ffmem_free(mi);
		return NULL;
	}
	mi->trk = d->trk;
	return mi;
}

static void mix_in_close(void *ctx)
{
	mix_in *mi = ctx;
	if (mi->m != NULL)
		mix_input_closed(mi->m, mi);
	ffmem_free(mi);
}

static int mix_in_write(void *ctx, fmed_filt *d)
{
	uint n;
	mix_in *mi = ctx;

	if (mix_err(mi->m))
		return FMED_RERR;

	switch (mi->state) {
	case 0:
		d->audio.convfmt.format = pcmfmt.format;
		d->audio.convfmt.ileaved = 1;
		mi->state = 1;
		return FMED_RMORE;

	case 1:
		if (pcmfmt.format != d->audio.convfmt.format
			|| pcmfmt.channels != d->audio.convfmt.channels
			|| pcmfmt.sample_rate != d->audio.convfmt.sample_rate) {
			errlog(core, d->trk, "mixer", "input format doesn't match output");
			mix_seterr(mi->m);
			return FMED_RERR;
		}
		pcmfmt.ileaved = d->audio.convfmt.ileaved;
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

const fmed_filter fmed_mix_in = { mix_in_open, mix_in_write, mix_in_close };


static void* mix_open(fmed_filt *d)
{
	mxr *m = ffmem_tcalloc1(mxr);
	if (m == NULL) {
		errlog(core, d->trk, "mixer", "%s", ffmem_alloc_S);
		return NULL;
	}

	if (NULL == ffstr_alloc(&m->data, DATA_SIZE)) {
		errlog(core, d->trk, "mixer", "%s", ffmem_alloc_S);
		ffmem_free(m);
		return NULL;
	}
	ffmem_zero(m->data.ptr, DATA_SIZE);

	m->trk = d->trk;
	fflist_init(&m->inputs);
	m->first = 1;
	m->sampsize = ffpcm_size(pcmfmt.format, pcmfmt.channels);

	ffpcm_fmtcopy(&d->audio.fmt, &pcmfmt);
	d->audio.fmt.ileaved = 1;

	m->trk_count = fmed_getval("mix_tracks");

	mx = m;
	d->datatype = "pcm";
	return m;
}

static void mix_close(void *ctx)
{
	mxr *m = ctx;
	mix_in *mi;

	_FFLIST_WALK(&m->inputs, mi, sib) {
		mi->m = NULL;
		if (mi->more) {
			mi->more = 0;
			track->cmd(mi->trk, FMED_TRACK_WAKE);
		}
	}
	ffstr_free(&m->data);
	ffmem_free(m);
	mx = NULL;
}

static void mix_seterr(mxr *m)
{
	if (m->err)
		return;
	m->err = 1;
	track->cmd(m->trk, FMED_TRACK_WAKE);
}

static ffbool mix_input_opened(mxr *m, mix_in *mi)
{
	if (m->err)
		return 0;

	fflist_ins(&m->inputs, &mi->sib);
	FF_ASSERT(m->inputs.len <= m->trk_count);
	mi->m = m;
	dbglog(m->trk, "input opened: %p  [%u]"
		, mi, (int)m->inputs.len);
	return 1;
}

static void mix_input_closed(mxr *m, mix_in *mi)
{
	fflist_rm(&m->inputs, &mi->sib);
	FF_ASSERT(m->trk_count != 0);
	m->trk_count--;
	if (mi->filled)
		m->filled--;
	if (m->filled == m->trk_count) {
		track->cmd(m->trk, FMED_TRACK_WAKE);
	}
	dbglog(m->trk, "input closed: %p  [%u]"
		, mi, m->trk_count);
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
			track->cmd(m->trk, FMED_TRACK_WAKE);
	}

	dbglog(m->trk, "added more data: +%u  offset:%xu  [%u/%u]"
		, n, off - n, m->filled, m->trk_count);
	return n;
}

static int mix_read(void *ctx, fmed_filt *d)
{
	mxr *m = ctx;
	mix_in *mi;

	if (m->err)
		return FMED_RERR;

	if (m->first) {
		m->first = 0;
		return FMED_RASYNC;
	}

	if (m->clear) {
		m->clear = 0;
		ffmem_zero(m->data.ptr, DATA_SIZE);
		m->data.len = 0;
		m->filled = 0;
		_FFLIST_WALK(&m->inputs, mi, sib) {
			mi->off = 0;
			mi->filled = 0;
		}

	} else if (m->data.len != 0 && m->filled == m->trk_count) {
		d->out = m->data.ptr;
		d->outlen = m->data.len;
		d->audio.pos += d->outlen / m->sampsize;
		m->clear = 1;
		return FMED_RDATA;
	}


	if (m->trk_count == 0) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	//notify those streams that have more output
	_FFLIST_WALK(&m->inputs, mi, sib) {
		if (mi->more) {
			mi->more = 0;
			track->cmd(mi->trk, FMED_TRACK_WAKE);
		}
	}

	return FMED_RASYNC;
}

const fmed_filter fmed_mix_out = { mix_open, mix_read, mix_close };
