/** MPEG input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/id3.h>
#include <FF/audio/mpeg.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>


static const fmed_core *core;

typedef struct fmed_mpeg {
	ffid3 id3;
	ffstr title
		, artist;
	uint mpgoff;
	unsigned id32done :1
		, done :1;
} fmed_mpeg;


//FMEDIA MODULE
static const fmed_filter* mpeg_iface(const char *name);
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


static const fmed_filter* mpeg_iface(const char *name)
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
	fmed_mpeg *m = ffmem_tcalloc1(fmed_mpeg);
	if (m == NULL)
		return NULL;

	ffid3_parseinit(&m->id3);
	return m;
}

static void mpeg_close(void *ctx)
{
	fmed_mpeg *m = ctx;
	ffstr_free(&m->title);
	ffstr_free(&m->artist);
	ffid3_parsefin(&m->id3);
	ffmem_free(m);
}

static int mpeg_meta(fmed_mpeg *m, fmed_filt *d)
{
	ffstr3 val = {0};
	const char *name, *v;

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
				, ffid3_size(&m->id3.h) - m->id3.size
				, (uint)m->id3.h.ver[0], (uint)m->id3.h.ver[1], (uint)m->id3.h.flags
				, ffid3_size(&m->id3.h));
			break;

		case FFID3_RMORE:
			return FMED_RMORE;

		case FFID3_RHDR:
			m->mpgoff = sizeof(ffid3_hdr) + ffid3_size(&m->id3.h);
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

			v = NULL;
			switch (ffid3_frame(&m->id3.fr)) {

			case FFID3_TITLE:
				name = "meta_title";
				ffstr_free(&m->title);
				if (NULL == (m->title.ptr = ffsz_alcopy(val.ptr, val.len)))
					break;
				v = m->title.ptr;
				break;

			case FFID3_ARTIST:
				name = "meta_artist";
				ffstr_free(&m->artist);
				if (NULL == (m->artist.ptr = ffsz_alcopy(val.ptr, val.len)))
					break;
				v = m->artist.ptr;
				break;
			}

			if (v != NULL)
				d->track->setvalstr(d->trk, name, v);

			ffarr_free(&val);
			break;
		}
	}

	return 0; //unreachable
}

static int mpeg_process(void *ctx, fmed_filt *d)
{
	fmed_mpeg *m = ctx;
	const ffmpg_hdr *mh;
	uint64 total_size, ms;

	if (m->done) {
		d->out = NULL;
		d->outlen = 0;
		errlog(core, d->trk, "mpeg", "decoding isn't supported");
		return FMED_RERR;
	}

	if (!m->id32done) {
		int r = mpeg_meta(m, d);
		if (r != FMED_RDONE)
			return r;
		m->id32done = 1;
	}

	mh = (void*)d->data;
	if (d->datalen < sizeof(ffmpg_hdr) || !ffmpg_valid(mh)) {
		errlog(core, d->trk, "mpeg", "invalid MPEG header at offset %u", m->mpgoff);
		return FMED_RERR;
	}

	d->track->setval(d->trk, "bitrate", ffmpg_bitrate(mh));
	d->track->setval(d->trk, "pcm_format", FFPCM_16LE);
	d->track->setval(d->trk, "pcm_channels", ffmpg_channels(mh));
	d->track->setval(d->trk, "pcm_sample_rate", ffmpg_sample_rate(mh));

	total_size = d->track->getval(d->trk, "total_size");
	if (total_size != FMED_NULL) {
		total_size -= m->mpgoff;
		ms = total_size * 1000 / (ffmpg_bitrate(mh) / 8);
		d->track->setval(d->trk, "total_samples", ffpcm_samples(ms, ffmpg_sample_rate(mh)));
	}
	m->done = 1;
	d->outlen = 0;
	return FMED_ROK;
}
