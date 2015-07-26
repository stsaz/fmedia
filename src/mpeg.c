/** MPEG input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/id3.h>
#include <FF/audio/mpeg.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>


static const fmed_core *core;

static const char *const metanames[] = {
	NULL
	, "meta_comment"
	, "meta_album"
	, "meta_genre"
	, "meta_title"
	, NULL
	, "meta_artist"
	, NULL
	, "meta_tracknumber"
	, "meta_date"
};

typedef struct fmed_mpeg {
	ffid3 id3;
	ffmpg mpg;
	ffstr title
		, artist;
	char *meta[FFCNT(metanames)];
	uint state;
} fmed_mpeg;


//FMEDIA MODULE
static const void* mpeg_iface(const char *name);
static int mpeg_sig(uint signo);
static void mpeg_destroy(void);
static const fmed_mod fmed_mpeg_mod = {
	&mpeg_iface, &mpeg_sig, &mpeg_destroy
};

//DECODE
static void* mpeg_open(fmed_filt *d);
static void mpeg_close(void *ctx);
static int mpeg_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mpeg_input = {
	&mpeg_open, &mpeg_process, &mpeg_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_mpeg_mod;
}


static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_mpeg_input;
	return NULL;
}

static int mpeg_sig(uint signo)
{
	return 0;
}

static void mpeg_destroy(void)
{
}


static void* mpeg_open(fmed_filt *d)
{
	int64 total_size;
	fmed_mpeg *m = ffmem_tcalloc1(fmed_mpeg);
	if (m == NULL)
		return NULL;
	ffmpg_init(&m->mpg);

	if (FMED_NULL != (total_size = fmed_getval("total_size")))
		m->mpg.total_size = total_size;

	ffid3_parseinit(&m->id3);
	return m;
}

static void mpeg_close(void *ctx)
{
	fmed_mpeg *m = ctx;
	uint i;
	for (i = 0;  i < FFCNT(m->meta);  i++) {
		ffmem_safefree(m->meta[i]);
	}
	ffstr_free(&m->title);
	ffstr_free(&m->artist);
	ffid3_parsefin(&m->id3);
	ffmpg_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_meta(fmed_mpeg *m, fmed_filt *d)
{
	ffstr3 val = {0};
	uint tag;

	for (;;) {
		size_t len = d->datalen;
		int r = ffid3_parse(&m->id3, d->data, &len);
		d->data += len;
		d->datalen -= len;

		switch (r) {
		case FFID3_RDONE:
			ffid3_parsefin(&m->id3);
			return FMED_RDONE;

		case FFID3_RERR:
			errlog(core, d->trk, "mpeg", "id3: parse (offset: %u): ID3v2.%u.%u, flags: %u, size: %u"
				, sizeof(ffid3_hdr) + ffid3_size(&m->id3.h) - m->id3.size
				, (uint)m->id3.h.ver[0], (uint)m->id3.h.ver[1], (uint)m->id3.h.flags
				, ffid3_size(&m->id3.h));
			break;

		case FFID3_RMORE:
			return FMED_RMORE;

		case FFID3_RHDR:
			m->mpg.dataoff = sizeof(ffid3_hdr) + ffid3_size(&m->id3.h);

			dbglog(core, d->trk, "mpeg", "id3: ID3v2.%u.%u, size: %u"
				, (uint)m->id3.h.ver[0], (uint)m->id3.h.ver[1], ffid3_size(&m->id3.h));
			break;

		case FFID3_RFRAME:
			switch (ffid3_frame(&m->id3.fr)) {
			case FFID3_PICTURE:
			case FFID3_COMMENT:
				m->id3.flags &= ~FFID3_FWHOLE;
				break;

			default:
				m->id3.flags |= FFID3_FWHOLE;
			}
			break;

		case FFID3_RDATA:
			if (!(m->id3.flags & FFID3_FWHOLE))
				break;

			if (0 > ffid3_getdata(m->id3.data.ptr, m->id3.data.len, m->id3.txtenc, 0, &val)) {
				errlog(core, d->trk, "mpeg", "id3: get frame data");
				break;
			}
			dbglog(core, d->trk, "mpeg", "tag: %*s: %S", (size_t)4, m->id3.fr.id, &val);

			tag = ffid3_frame(&m->id3.fr);
			if (tag < FFCNT(metanames) && metanames[tag] != NULL && val.len != 0) {
				ffmem_safefree(m->meta[tag]);
				if (NULL == (m->meta[tag] = ffsz_alcopy(val.ptr, val.len)))
					return FMED_RERR;
				d->track->setvalstr(d->trk, metanames[tag], m->meta[tag]);
			}

			if (tag == FFID3_LENGTH && m->id3.data.len != 0) {
				uint64 dur;
				if (m->id3.data.len == ffs_toint(m->id3.data.ptr, m->id3.data.len, &dur, FFS_INT64))
					m->mpg.total_len = dur;
			}

			ffarr_free(&val);
			break;
		}
	}

	return 0; //unreachable
}

static int mpeg_process(void *ctx, fmed_filt *d)
{
	enum { I_META, I_HDR, I_DATA };
	fmed_mpeg *m = ctx;
	int r;
	int64 seek_time, until_time;

again:
	switch (m->state) {
	case I_META:
		r = mpeg_meta(m, d);
		if (r != FMED_RDONE)
			return r;
		m->state = I_HDR;
		//break;

	case I_HDR:
		break;

	case I_DATA:
		if (FMED_NULL != (seek_time = fmed_popval("seek_time")))
			ffmpg_seek(&m->mpg, ffpcm_samples(seek_time, m->mpg.fmt.sample_rate));
		break;
	}

	m->mpg.data = d->data;
	m->mpg.datalen = d->datalen;

	for (;;) {
		r = ffmpg_decode(&m->mpg);

		switch (r) {
		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMPG_RHDR:
			fmed_setpcm(d, &m->mpg.fmt);
			fmed_setval("pcm_ileaved", 0);
			fmed_setval("bitrate", m->mpg.bitrate);
			fmed_setval("total_samples", m->mpg.total_samples);
			m->state = I_DATA;
			goto again;

		case FFMPG_RSEEK:
			fmed_setval("input_seek", ffmpg_seekoff(&m->mpg));
			return FMED_RMORE;

		case FFMPG_RWARN:
			errlog(core, d->trk, "mpeg", "warning: ffmpg_decode(): %s", ffmpg_errstr(&m->mpg));
			break;

		case FFMPG_RERR:
		default:
			errlog(core, d->trk, "mpeg", "ffmpg_decode(): %s", ffmpg_errstr(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	if (FMED_NULL != (until_time = d->track->getval(d->trk, "until_time"))) {
		uint64 until_samples = until_time * m->mpg.fmt.sample_rate / 1000;
		if (until_samples <= ffmpg_cursample(&m->mpg)) {
			dbglog(core, d->trk, "mpeg", "until_time is reached");
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
	}

	d->data = m->mpg.data;
	d->datalen = m->mpg.datalen;
	d->outni = (void**)m->mpg.pcm;
	d->outlen = m->mpg.pcmlen;
	fmed_setval("current_position", ffmpg_cursample(&m->mpg));

	dbglog(core, d->trk, "mpeg", "output: %L PCM samples"
		, d->outlen / ffpcm_size1(&m->mpg.fmt));
	return FMED_ROK;
}
