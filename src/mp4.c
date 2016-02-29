/** MP4 (ALAC) input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/mp4.h>
#include <FF/audio/alac.h>


static const fmed_core *core;
static const fmed_queue *qu;

static const byte mp4_meta_ids[] = {
	FFMP4_COMMENT,
	FFMP4_ALBUM,
	FFMP4_GENRE,
	FFMP4_TITLE,
	FFMP4_ARTIST,
	FFMP4_ALBUMARTIST,
	FFMP4_TRACKNO,
	FFMP4_TRACKTOTAL,
	FFMP4_YEAR,
	FFMP4_TOOL,
};

static const char *const mp4_metanames[] = {
	"comment",
	"album",
	"genre",
	"title",
	"artist",
	"albumartist",
	"tracknumber",
	"tracktotal",
	"date",
	"vendor",
};

typedef struct mp4 {
	ffmp4 mp;
	uint state;

	ffalac alac;
} mp4;


//FMEDIA MODULE
static const void* mp4_iface(const char *name);
static int mp4_sig(uint signo);
static void mp4_destroy(void);
static const fmed_mod fmed_mp4_mod = {
	&mp4_iface, &mp4_sig, &mp4_destroy
};

//DECODE
static void* mp4_in_create(fmed_filt *d);
static void mp4_in_free(void *ctx);
static int mp4_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mp4_input = {
	&mp4_in_create, &mp4_in_decode, &mp4_in_free
};

static void mp4_meta(mp4 *m, fmed_filt *d);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_mp4_mod;
}


static const void* mp4_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_mp4_input;
	return NULL;
}

static int mp4_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void mp4_destroy(void)
{
}


static void* mp4_in_create(fmed_filt *d)
{
	int64 val;
	mp4 *m = ffmem_tcalloc1(mp4);
	if (m == NULL)
		return NULL;

	ffmp4_init(&m->mp);

	if (FMED_NULL != (val = fmed_getval("total_size")))
		m->mp.total_size = val;

	return m;
}

static void mp4_in_free(void *ctx)
{
	mp4 *m = ctx;
	ffalac_close(&m->alac);
	ffmp4_close(&m->mp);
	ffmem_free(m);
}

static void mp4_meta(mp4 *m, fmed_filt *d)
{
	uint tag = 0;
	ffstr name, val;
	if (-1 == (tag = ffint_find1(mp4_meta_ids, FFCNT(mp4_meta_ids), m->mp.tag)))
		return;
	ffstr_setz(&name, mp4_metanames[tag]);
	val = m->mp.tagval;

	dbglog(core, d->trk, "mp4", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int mp4_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA, I_DECODE, };
	mp4 *m = ctx;
	int r;
	int64 val;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	m->mp.data = d->data;
	m->mp.datalen = d->datalen;

	for (;;) {

	switch (m->state) {

	case I_DATA:
		if (FMED_NULL != (val = fmed_popval("seek_time")))
			ffmp4_seek(&m->mp, ffpcm_samples(val, m->mp.fmt.sample_rate));

	case I_HDR:
		r = ffmp4_read(&m->mp);
		switch (r) {
		case FFMP4_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "mp4", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMP4_RHDR:
			d->track->setvalstr(d->trk, "pcm_decoder", ffmp4_codec(m->mp.codec));
			fmed_setval("pcm_format", m->mp.fmt.format);
			fmed_setval("pcm_channels", m->mp.fmt.channels);
			fmed_setval("pcm_sample_rate", m->mp.fmt.sample_rate);

			fmed_setval("total_samples", ffmp4_totalsamples(&m->mp));

			if (FMED_NULL != fmed_getval("input_info"))
				break;

			if (m->mp.codec != FFMP4_ALAC) {
				errlog(core, d->trk, "mp4", "%s: decoding unsupported", ffmp4_codec(m->mp.codec));
				return FMED_RERR;
			}

			if (0 != ffalac_open(&m->alac, m->mp.out, m->mp.outlen)) {
				errlog(core, d->trk, "mp4", "ffalac_open(): %s", ffalac_errstr(&m->alac));
				return FMED_RERR;
			}
			if (0 != memcmp(&m->alac.fmt, &m->mp.fmt, sizeof(m->alac.fmt))) {
				errlog(core, d->trk, "mp4", "ALAC: audio format doesn't match with format from MP4");
				return FMED_RERR;
			}

			fmed_setval("bitrate", m->alac.bitrate);
			fmed_setval("pcm_ileaved", 1);
			break;

		case FFMP4_RTAG:
			mp4_meta(m, d);
			break;

		case FFMP4_RMETAFIN:
			if (FMED_NULL != fmed_getval("input_info"))
				return FMED_ROK;

			m->state = I_DATA;
			continue;

		case FFMP4_RDATA:
			m->alac.data = m->mp.out;
			m->alac.datalen = m->mp.outlen;
			m->state = I_DECODE;
			continue;

		case FFMP4_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMP4_RSEEK:
			fmed_setval("input_seek", m->mp.off);
			return FMED_RMORE;

		case FFMP4_RWARN:
			warnlog(core, d->trk, "mp4", "ffmp4_read(): at offset 0x%xU: %s"
				, m->mp.off, ffmp4_errstr(&m->mp));
			break;

		case FFMP4_RERR:
			errlog(core, d->trk, "mp4", "ffmp4_read(): %s", ffmp4_errstr(&m->mp));
			return FMED_RERR;
		}
		break;

	case I_DECODE:
		r = ffalac_decode(&m->alac);
		if (r == FFALAC_RERR) {
			errlog(core, d->trk, "mp4", "ffalac_decode(): %s", ffalac_errstr(&m->alac));
			return FMED_RERR;
		}
		if (r == FFALAC_RMORE) {
			m->state = I_DATA;
			continue;
		}
		dbglog(core, d->trk, "mp4", "ALAC: decoded %u samples (%d)"
			, m->alac.pcmlen / ffpcm_size1(&m->alac.fmt), r);
		fmed_setval("current_position", ffmp4_cursample(&m->mp));

		d->data = (void*)m->mp.data;
		d->datalen = m->mp.datalen;
		d->out = m->alac.pcm;
		d->outlen = m->alac.pcmlen;
		return FMED_RDATA;
	}

	}

	//unreachable
}
