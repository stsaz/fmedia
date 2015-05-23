/** OGG Vorbis input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/ogg.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FFOS/error.h>


static const fmed_core *core;
static uint status;

typedef struct fmed_ogg {
	ffogg og;
	ffarr ileav;
	uint state;
	uint byte_rate;
	uint64 curpos;
	uint64 seekpos;
	uint64 data_off;
	uint64 first_gpos;
	unsigned seek :1;
} fmed_ogg;

enum {
	conf_seek = 1
};

//FMEDIA MODULE
static const fmed_filter* ogg_iface(const char *name);
static int ogg_sig(uint signo);
static void ogg_destroy(void);
static const fmed_mod fmed_ogg_mod = {
	&ogg_iface, &ogg_sig, &ogg_destroy
};

//DECODE
static void* ogg_open(fmed_filt *d);
static void ogg_close(void *ctx);
static int ogg_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_ogg_input = {
	&ogg_open, &ogg_decode, &ogg_close
};

static int ogg_parse(fmed_ogg *o, fmed_filt *d);
static int ogg_duration(fmed_ogg *o, fmed_filt *d);
static void ogg_interleave(char *dst, const char *src, uint channels, size_t samples);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_ogg_mod;
}


static const fmed_filter* ogg_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_ogg_input;
	return NULL;
}

static int ogg_sig(uint signo)
{
	switch (signo) {
	case FMED_STOP:
		status = 1;
		break;
	}
	return 0;
}

static void ogg_destroy(void)
{
}


static void* ogg_open(fmed_filt *d)
{
	fmed_ogg *o = ffmem_tcalloc1(fmed_ogg);
	if (o == NULL)
		return NULL;
	ffogg_init(&o->og);
	o->state = 3;
	return o;
}

static void ogg_close(void *ctx)
{
	fmed_ogg *o = ctx;
	ffogg_close(&o->og);
	ffarr_free(&o->ileav);
	ffmem_free(o);
}

static int ogg_parse(fmed_ogg *o, fmed_filt *d)
{
	int r;
	const char *name;

	o->og.data = d->data;
	o->og.datalen = d->datalen;

	for (;;) {
		r = ffogg_open(&o->og);
		if (r < 0) {
			errlog(core, d->trk, "ogg", "ffogg_open()");
			return FMED_RERR;
		}

		switch (r) {
		case FFOGG_RDONE:
			dbglog(core, d->trk, "ogg", "vendor: %s", o->og.vcmt.vendor);
			goto done;

		case FFOGG_RMORE:
			return FMED_RMORE;

		case FFOGG_RTAG:
			dbglog(core, d->trk, "ogg", "%S: %S", &o->og.tagname, &o->og.tagval);

			name = NULL;
			switch (ffogg_tag(o->og.tagname.ptr, o->og.tagname.len)) {
			case FFOGG_TITLE:
				name = "meta_title";
				break;

			case FFOGG_ARTIST:
				name = "meta_artist";
				break;
			}

			if (name != NULL)
				d->track->setvalstr(d->trk, name, o->og.tagval.ptr);
			break;
		}
	}

done:
	dbglog(core, d->trk, "ogg", "granule pos: %U", ffogg_granulepos(&o->og));

	o->first_gpos = ffogg_granulepos(&o->og);
	d->track->setval(d->trk, "pcm_format", FFPCM_FLOAT);
	d->track->setval(d->trk, "pcm_channels", ffogg_channels(&o->og));
	d->track->setval(d->trk, "pcm_sample_rate", ffogg_rate(&o->og));

	o->data_off += d->datalen - o->og.datalen;

	if (conf_seek) {
		uint64 input_size = d->track->getval(d->trk, "total_size");
		if (input_size != FMED_NULL) {
			//read the last 4k to determine total duration
			d->track->setval(d->trk, "input_seek", input_size - 4096);
			o->state = 2;
			return FMED_RMORE;
		}
	}

	d->data = o->og.data;
	d->datalen = o->og.datalen;
	o->state = 0;
	return FMED_ROK;
}

static int ogg_duration(fmed_ogg *o, fmed_filt *d)
{
	int r;
	ffogg og2;
	uint64 dur, trk_size;
	uint brate;

	ffogg_init(&og2);
	og2.data = d->data;
	og2.datalen = d->datalen;
	r = ffogg_pageseek(&og2);
	if (r <= 0) {
		errlog(core, d->trk, "ogg", "pageseek failed");
		goto done;
	}

	dur = (ffogg_granulepos(&og2) - o->first_gpos);
	d->track->setval(d->trk, "total_samples", dur);

	trk_size = d->track->getval(d->trk, "total_size");
	brate = ffogg_bitrate(&o->og, dur, (trk_size != FMED_NULL) ? trk_size : 0);
	o->byte_rate = brate / 8;
	d->track->setval(d->trk, "bitrate", brate);

done:
	ffogg_close(&og2);
	d->datalen = 0;
	o->state = 0;
	return FMED_ROK;
}

static int ogg_decode(void *ctx, fmed_filt *d)
{
	fmed_ogg *o = ctx;
	int r;
	int64 seek_time, until_time;

	if (status == 1) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	switch (o->state) {
	case 3:
		r = ogg_parse(o, d);
		if (r != FMED_ROK)
			return r;
		break;

	case 2:
		r = ogg_duration(o, d);
		if (r != FMED_ROK)
			return r;
		if (FMED_NULL == d->track->getval(d->trk, "seek_time")) {
			d->track->setval(d->trk, "input_seek", o->data_off);
			return FMED_RMORE;
		}
		break;
	}

	seek_time = d->track->popval(d->trk, "seek_time");
	if (seek_time != FMED_NULL) {
		uint64 seek_bytes = o->byte_rate * seek_time / 1000;
		dbglog(core, d->trk, "ogg", "seeking to %U sec (byte %U)", seek_time / 1000, seek_bytes);
		d->track->setval(d->trk, "input_seek", seek_bytes - 4096);
		o->seek = 1;
		o->seekpos = ffpcm_samples(seek_time, ffogg_rate(&o->og));
		return FMED_RMORE;
	}

	if (o->seek) {
		//take granule pos from the previous page and then shift to the next page that is our target
		o->seek = 0;
		o->og.data = d->data;
		o->og.datalen = d->datalen;
		r = ffogg_pageseek(&o->og);
		//note: we assume that all pages are aligned to 4k and input block size is larger
		if (r <= 0) {
			errlog(core, d->trk, "ogg", "pageseek failed");
			return FMED_RERR;
		}
		d->data = o->og.data;
		d->datalen = o->og.datalen;

		o->curpos = ffogg_granulepos(&o->og);
		dbglog(core, d->trk, "ogg", "seek diff: %D (%D ms)"
			, o->curpos - o->seekpos, ffpcm_time(o->curpos - o->seekpos, ffogg_rate(&o->og)));
		o->seekpos = 0;

		o->og.data = d->data;
		o->og.datalen = d->datalen;
		r = ffogg_pageseek(&o->og);
		if (r <= 0) {
			errlog(core, d->trk, "ogg", "pageseek failed");
			return FMED_RERR;
		}
	}

	until_time = d->track->getval(d->trk, "until_time");
	if (until_time != FMED_NULL) {
		uint64 until_samples = (until_time / 1000) * ffogg_rate(&o->og);
		if (until_samples <= o->curpos) {
			dbglog(core, d->trk, "ogg", "until_time is reached");
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
	}

	o->og.data = d->data;
	o->og.datalen = d->datalen;
	r = ffogg_decode(&o->og);
	if (r < 0) {
		errlog(core, d->trk, "ogg", "ffogg_decode()");
		return FMED_RERR;
	}
	if (r == FFOGG_RMORE)
		return FMED_RMORE;

	dbglog(core, d->trk, "ogg", "decoded %u PCM samples, page: %u, granule pos: %U"
		, o->og.nsamples, ogg_page_pageno(&o->og.opg), ffogg_granulepos(&o->og));
	o->curpos += o->og.nsamples;
	d->track->setval(d->trk, "current_position", o->curpos);

	d->data = o->og.data;
	d->datalen = o->og.datalen;

	if (NULL == _ffarr_realloc(&o->ileav, o->og.nsamples, ffpcm_size(FFPCM_FLOAT, ffogg_channels(&o->og)))) {
		syserrlog(core, d->trk, "ogg", "%e", FFERR_BUFALOC);
		return FMED_RERR;
	}
	ogg_interleave(o->ileav.ptr, o->og.pcm, ffogg_channels(&o->og), o->og.nsamples);

	d->out = o->ileav.ptr;
	d->outlen = o->og.pcmlen;

	if ((d->flags & FMED_FLAST) && d->datalen == 0)
		return FMED_RDONE;
	return FMED_ROK;
}

/*
pcm[0][..] - left,  pcm[1][..] - right
interleave: pcm[0,2..] - left */
static void ogg_interleave(char *dst, const char *src, uint channels, size_t samples)
{
	uint *out = (void*)dst;
	const uint **in = (void*)src;
	int n, ich, iff;

	for (ich = 0;  ich != channels;  ich++) {
		iff = ich;

		for (n = 0;  n < samples;  n++) {
			out[iff] = in[ich][n];
			iff += channels;
		}
	}
}
