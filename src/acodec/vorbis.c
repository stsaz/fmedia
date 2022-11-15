/** Vorbis input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <acodec/alib3-bridge/vorbis.h>
#include <format/mmtag.h>

static const fmed_core *core;
static const fmed_queue *qu;

#include <acodec/vorbis-enc.h>

//FMEDIA MODULE
static const void* vorbis_iface(const char *name);
static int vorbis_conf(const char *name, fmed_conf_ctx *ctx);
static int vorbis_sig(uint signo);
static void vorbis_destroy(void);
static const fmed_mod fmed_vorbis_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&vorbis_iface, &vorbis_sig, &vorbis_destroy, &vorbis_conf
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_vorbis_mod;
}

static const fmed_filter vorbis_input;
static const void* vorbis_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &vorbis_input;
	else if (!ffsz_cmp(name, "encode"))
		return &vorbis_output;
	return NULL;
}

static int vorbis_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return vorbis_out_config(ctx);
	return -1;
}

static int vorbis_sig(uint signo)
{
	switch (signo) {
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

		v->state++;
		break;

	case R_DATA1:
		if (d->input_info)
			return FMED_RDONE;

		if ((int64)d->audio.total != FMED_NULL && d->audio.total != 0)
			v->vorbis.total_samples = d->audio.total;
		v->state = R_DATA;
		// break

	case R_DATA:
		if ((d->flags & FMED_FFWD) && (int64)d->audio.seek != FMED_NULL) {
			uint64 seek = ffpcm_samples(d->audio.seek, ffvorbis_rate(&v->vorbis));
			ffvorbis_seek(&v->vorbis, seek);
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
		d->audio.fmt.ileaved = 0;
		d->datatype = "pcm";
		return FMED_RMORE;

	case FFVORBIS_RHDRFIN:
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

static const fmed_filter vorbis_input = { vorbis_open, vorbis_in_decode, vorbis_close };
