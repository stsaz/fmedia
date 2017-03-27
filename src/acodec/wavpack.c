/** WavPack input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wavpack.h>
#include <FF/data/mmtag.h>
#include <FF/number.h>


static const fmed_core *core;
static const fmed_queue *qu;

typedef struct wvpk {
	ffwvpack wp;
	int64 abs_seek;
	uint state;
} wvpk;


//FMEDIA MODULE
static const void* wvpk_iface(const char *name);
static int wvpk_sig(uint signo);
static void wvpk_destroy(void);
static const fmed_mod fmed_wvpk_mod = {
	&wvpk_iface, &wvpk_sig, &wvpk_destroy
};

//DECODE
static void* wvpk_in_create(fmed_filt *d);
static void wvpk_in_free(void *ctx);
static int wvpk_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_wvpk_input = {
	&wvpk_in_create, &wvpk_in_decode, &wvpk_in_free
};

static void wvpk_meta(wvpk *w, fmed_filt *d);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_wvpk_mod;
}


static const void* wvpk_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_wvpk_input;
	return NULL;
}

static int wvpk_sig(uint signo)
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

static void wvpk_destroy(void)
{
}


static void* wvpk_in_create(fmed_filt *d)
{
	wvpk *w = ffmem_tcalloc1(wvpk);
	if (w == NULL)
		return NULL;
	w->wp.options = FFWVPK_O_ID3V1 | FFWVPK_O_APETAG;

	if ((int64)d->input.size != FMED_NULL)
		w->wp.total_size = d->input.size;
	return w;
}

static void wvpk_in_free(void *ctx)
{
	wvpk *w = ctx;
	ffwvpk_close(&w->wp);
	ffmem_free(w);
}

static void wvpk_meta(wvpk *w, fmed_filt *d)
{
	ffstr name, val;

	if (w->wp.is_apetag) {
		name = w->wp.apetag.name;
		if (FFAPETAG_FBINARY == (w->wp.apetag.flags & FFAPETAG_FMASK)) {
			dbglog(core, d->trk, "ape", "skipping binary tag: %S", &name);
			return;
		}
	}

	if (w->wp.tag != 0)
		ffstr_setz(&name, ffmmtag_str[w->wp.tag]);
	val = w->wp.tagval;
	dbglog(core, d->trk, "wvpk", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int wvpk_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	wvpk *w = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	w->wp.data = (byte*)d->data;
	w->wp.datalen = d->datalen;
	if (d->flags & FMED_FLAST)
		w->wp.fin = 1;

again:
	switch (w->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			ffwvpk_seek(&w->wp, w->abs_seek + ffpcm_samples(d->audio.seek, w->wp.fmt.sample_rate));
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	for (;;) {
		r = ffwvpk_decode(&w->wp);
		switch (r) {
		case FFWVPK_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "wvpk", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFWVPK_RHDR:
			d->track->setvalstr(d->trk, "pcm_decoder", "WavPack");
			ffpcm_fmtcopy(&d->audio.fmt, &w->wp.fmt);
			d->audio.fmt.ileaved = 1;
			d->audio.bitrate = ffwvpk_bitrate(&w->wp);

			if (d->audio.abs_seek != 0) {
				w->abs_seek = fmed_apos_samples(d->audio.abs_seek, w->wp.fmt.sample_rate);
			}

			d->audio.total = ffwvpk_total_samples(&w->wp) - w->abs_seek;
			break;

		case FFWVPK_RTAG:
			wvpk_meta(w, d);
			break;

		case FFWVPK_RHDRFIN:
			dbglog(core, d->trk, "wvpk", "version:%xu  lossless:%u  compression:%s  block-samples:%u  MD5:%16xb"
				, (int)w->wp.info.version, (int)w->wp.info.lossless
				, ffwvpk_comp_levelstr[w->wp.info.comp_level], (int)w->wp.info.block_samples
				, w->wp.info.md5);

			if (d->input_info)
				return FMED_ROK;

			w->state = I_DATA;
			if (w->abs_seek != 0)
				ffwvpk_seek(&w->wp, w->abs_seek);
			goto again;

		case FFWVPK_RDATA:
			goto data;

		case FFWVPK_RSEEK:
			d->input.seek = ffwvpk_seekoff(&w->wp);
			return FMED_RMORE;

		case FFWVPK_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFWVPK_RWARN:
			warnlog(core, d->trk, "wvpk", "ffwvpk_decode(): at offset %xU: %s"
				, w->wp.off, ffwvpk_errstr(&w->wp));
			break;

		case FFWVPK_RERR:
			errlog(core, d->trk, "wvpk", "ffwvpk_decode(): %s", ffwvpk_errstr(&w->wp));
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "wvpk", "decoded %L samples (%U)"
		, (size_t)w->wp.pcmlen / ffpcm_size1(&w->wp.fmt), ffwvpk_cursample(&w->wp));
	d->audio.pos = ffwvpk_cursample(&w->wp) - w->abs_seek;

	d->data = (void*)w->wp.data;
	d->datalen = w->wp.datalen;
	d->out = (void*)w->wp.pcm;
	d->outlen = w->wp.pcmlen;
	return FMED_RDATA;
}
