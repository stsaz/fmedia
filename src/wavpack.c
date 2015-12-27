/** WavPack input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/wavpack.h>
#include <FF/number.h>


static const fmed_core *core;
static const fmed_queue *qu;

// enum FFAPETAG_FIELD
static const char *const ape_metanames[] = {
	"meta_album",
	"meta_artist",
	"meta_comment",
	"meta_genre",
	"meta_title",
	"meta_tracknumber",
	"meta_date",
};

static const byte id3_meta_ids[] = {
	FFID3_COMMENT,
	FFID3_ALBUM,
	FFID3_GENRE,
	FFID3_TITLE,
	FFID3_ARTIST,
	FFID3_TRACKNO,
	FFID3_YEAR,
};

static const char *const id3_metanames[] = {
	"meta_comment",
	"meta_album",
	"meta_genre",
	"meta_title",
	"meta_artist",
	"meta_tracknumber",
	"meta_date",
};

typedef struct wvpk {
	ffwvpack wp;
	int64 abs_seek; // msec/cdframe/sample
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
	ffmem_init();
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
	int64 val;
	wvpk *w = ffmem_tcalloc1(wvpk);
	if (w == NULL)
		return NULL;
	w->wp.options = FFWVPK_O_ID3V1 | FFWVPK_O_APETAG;

	if (FMED_NULL != (val = fmed_getval("total_size")))
		w->wp.total_size = val;

	if (FMED_NULL != (val = fmed_getval("seek_time_abs")))
		w->abs_seek = val;
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
	uint tag = 0;
	ffstr name, val;
	const char *tagstr, *tagval;

	if (w->wp.is_apetag) {
		val = w->wp.apetag.val;
		tag = ffapetag_field(w->wp.apetag.name.ptr);
		if (tag < FFCNT(ape_metanames))
			ffstr_setz(&name, ape_metanames[tag]);
		else
			name = w->wp.apetag.name;

	} else {
		tag = ffint_find1(id3_meta_ids, FFCNT(id3_meta_ids), w->wp.id31tag.field);
		ffstr_setz(&name, id3_metanames[tag]);
		val = w->wp.id31tag.val;
	}

	dbglog(core, d->trk, "wvpk", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);

	if (w->wp.is_apetag) {
		if (tag >= FFCNT(ape_metanames))
			return;
		tagstr = ape_metanames[tag];

	} else
		tagstr = id3_metanames[tag];

	tagval = ffsz_alcopy(val.ptr, val.len);
	d->track->setvalstr4(d->trk, tagstr, tagval, FMED_TRK_FACQUIRE | FMED_TRK_FNO_OVWRITE);
}

static int wvpk_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	wvpk *w = ctx;
	int r;
	int64 seek_time;

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
		if (FMED_NULL != (seek_time = fmed_popval("seek_time")))
			ffwvpk_seek(&w->wp, w->abs_seek + ffpcm_samples(seek_time, w->wp.fmt.sample_rate));
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
			fmed_setval("pcm_format", w->wp.fmt.format);
			fmed_setval("pcm_channels", w->wp.fmt.channels);
			fmed_setval("pcm_sample_rate", w->wp.fmt.sample_rate);
			fmed_setval("pcm_ileaved", 1);
			fmed_setval("bitrate", ffwvpk_bitrate(&w->wp));

			if (w->abs_seek > 0)
				w->abs_seek = ffpcm_samples(w->abs_seek, w->wp.fmt.sample_rate);
			else if (w->abs_seek < 0)
				w->abs_seek = -w->abs_seek * w->wp.fmt.sample_rate / 75;

			fmed_setval("total_samples", ffwvpk_total_samples(&w->wp) - w->abs_seek);
			break;

		case FFWVPK_RTAG:
			wvpk_meta(w, d);
			break;

		case FFWVPK_RHDRFIN:
			dbglog(core, d->trk, "wvpk", "version:%xu  lossless:%u  compression:%s  block-samples:%u  MD5:%16xb"
				, (int)w->wp.info.version, (int)w->wp.info.lossless
				, ffwvpk_comp_levelstr[w->wp.info.comp_level], (int)w->wp.info.block_samples
				, w->wp.info.md5);

			if (FMED_NULL != fmed_getval("input_info"))
				return FMED_ROK;

			w->state = I_DATA;
			if (w->abs_seek != 0)
				ffwvpk_seek(&w->wp, w->abs_seek);
			goto again;

		case FFWVPK_RDATA:
			goto data;

		case FFWVPK_RSEEK:
			fmed_setval("input_seek", ffwvpk_seekoff(&w->wp));
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
	fmed_setval("current_position", ffwvpk_cursample(&w->wp) - w->abs_seek);

	d->data = (void*)w->wp.data;
	d->datalen = w->wp.datalen;
	d->out = (void*)w->wp.pcm;
	d->outlen = w->wp.pcmlen;
	return FMED_RDATA;
}
