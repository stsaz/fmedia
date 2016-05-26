/** APE input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/ape.h>


static const fmed_core *core;
static const fmed_queue *qu;

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
	"comment",
	"album",
	"genre",
	"title",
	"artist",
	"tracknumber",
	"date",
};

typedef struct ape {
	ffape ap;
	int64 abs_seek; // msec/cdframe/sample
	uint state;
} ape;


//FMEDIA MODULE
static const void* ape_iface(const char *name);
static int ape_sig(uint signo);
static void ape_destroy(void);
static const fmed_mod fmed_ape_mod = {
	&ape_iface, &ape_sig, &ape_destroy
};

//DECODE
static void* ape_in_create(fmed_filt *d);
static void ape_in_free(void *ctx);
static int ape_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_ape_input = {
	&ape_in_create, &ape_in_decode, &ape_in_free
};

static void ape_meta(ape *a, fmed_filt *d);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_ape_mod;
}


static const void* ape_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_ape_input;
	return NULL;
}

static int ape_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void ape_destroy(void)
{
}


static void* ape_in_create(fmed_filt *d)
{
	int64 total_size, val;
	ape *a = ffmem_tcalloc1(ape);
	if (a == NULL)
		return NULL;

	a->ap.options = FFAPE_O_ID3V1 | FFAPE_O_APETAG;

	if (FMED_NULL != (total_size = fmed_getval("total_size")))
		a->ap.total_size = total_size;

	if (FMED_NULL != (val = fmed_getval("seek_time_abs")))
		a->abs_seek = val;
	return a;
}

static void ape_in_free(void *ctx)
{
	ape *a = ctx;
	ffape_close(&a->ap);
	ffmem_free(a);
}

static void ape_meta(ape *a, fmed_filt *d)
{
	uint tag;
	ffstr name, val;

	if (a->ap.is_apetag) {
		name = a->ap.apetag.name;
		if (FFAPETAG_FBINARY == (a->ap.apetag.flags & FFAPETAG_FMASK)) {
			dbglog(core, d->trk, "ape", "skipping binary tag: %S", &name);
			return;
		}
	}

	tag = ffint_find1(id3_meta_ids, FFCNT(id3_meta_ids), a->ap.tag);
	if (tag >= 0)
		ffstr_setz(&name, id3_metanames[tag]);
	val = a->ap.tagval;
	dbglog(core, d->trk, "ape", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int ape_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	ape *a = ctx;
	int r;
	int64 seek_time;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	a->ap.data = d->data;
	a->ap.datalen = d->datalen;
	if (d->flags & FMED_FLAST)
		a->ap.fin = 1;

again:
	switch (a->state) {
	case I_HDR:
		break;

	case I_DATA:
		if (FMED_NULL != (seek_time = fmed_popval("seek_time")))
			ffape_seek(&a->ap, a->abs_seek + ffpcm_samples(seek_time, a->ap.info.fmt.sample_rate));
		break;
	}

	for (;;) {
		r = ffape_decode(&a->ap);
		switch (r) {
		case FFAPE_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "ape", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFAPE_RHDR:
			d->track->setvalstr(d->trk, "pcm_decoder", "APE");
			fmed_setval("pcm_format", a->ap.info.fmt.format);
			fmed_setval("pcm_channels", a->ap.info.fmt.channels);
			fmed_setval("pcm_sample_rate", a->ap.info.fmt.sample_rate);
			fmed_setval("pcm_ileaved", 1);

			if (a->abs_seek != 0) {
				if (a->abs_seek > 0)
					a->abs_seek = ffpcm_samples(a->abs_seek, a->ap.info.fmt.sample_rate);
				else
					a->abs_seek = -a->abs_seek * a->ap.info.fmt.sample_rate / 75;
			}

			fmed_setval("total_samples", ffape_totalsamples(&a->ap) - a->abs_seek);
			break;

		case FFAPE_RTAG:
			ape_meta(a, d);
			break;

		case FFAPE_RHDRFIN:
			dbglog(core, d->trk, "ape", "version:%u  compression:%s  blocksize:%u  MD5:%16xb  seek-table:%u  meta-length:%u"
				, a->ap.info.version, ffape_comp_levelstr[a->ap.info.comp_level], (int)a->ap.info.frame_blocks
				, a->ap.info.md5, (int)a->ap.info.seekpoints, (int)a->ap.froff);
			fmed_setval("bitrate", ffape_bitrate(&a->ap));

			if (FMED_NULL != fmed_getval("input_info"))
				return FMED_ROK;

			a->state = I_DATA;
			if (a->abs_seek != 0)
				ffape_seek(&a->ap, a->abs_seek);
			goto again;

		case FFAPE_RDATA:
			goto data;

		case FFAPE_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFAPE_RSEEK:
			fmed_setval("input_seek", a->ap.off);
			return FMED_RMORE;

		case FFAPE_RWARN:
			warnlog(core, d->trk, "ape", "ffape_decode(): at offset 0x%xU: %s"
				, a->ap.off, ffape_errstr(&a->ap));
			break;

		case FFAPE_RERR:
			errlog(core, d->trk, "ape", "ffape_decode(): %s", ffape_errstr(&a->ap));
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "ape", "decoded %L samples (%U)"
		, a->ap.pcmlen / ffpcm_size1(&a->ap.info.fmt), ffape_cursample(&a->ap));
	fmed_setval("current_position", ffape_cursample(&a->ap) - a->abs_seek);

	d->data = (void*)a->ap.data;
	d->datalen = a->ap.datalen;
	d->out = (void*)a->ap.pcm;
	d->outlen = a->ap.pcmlen;
	return FMED_RDATA;
}
