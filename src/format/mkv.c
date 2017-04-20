/** MKV input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/data/mkv.h>
#include <FF/audio/pcm.h>
#include <FF/data/mmtag.h>
#include <FF/array.h>
#include <FFOS/error.h>


static const fmed_core *core;
static const fmed_queue *qu;

typedef struct fmed_mkv {
	ffmkv mkv;
	ffmkv_vorbis mkv_vorbis;
	uint state;
} fmed_mkv;


//FMEDIA MODULE
static const void* mkv_iface(const char *name);
static int mkv_sig(uint signo);
static void mkv_destroy(void);
static const fmed_mod fmed_mkv_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mkv_iface, &mkv_sig, &mkv_destroy
};

//INPUT
static void* mkv_open(fmed_filt *d);
static void mkv_close(void *ctx);
static int mkv_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mkv_input = {
	&mkv_open, &mkv_process, &mkv_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mkv_mod;
}


static const void* mkv_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_mkv_input;
	return NULL;
}

static int mkv_sig(uint signo)
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

static void mkv_destroy(void)
{
}


static void* mkv_open(fmed_filt *d)
{
	fmed_mkv *m = ffmem_tcalloc1(fmed_mkv);
	if (m == NULL) {
		errlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
		return NULL;
	}
	ffmkv_open(&m->mkv);
	m->mkv.options = FFMKV_O_TAGS;
	return m;
}

static void mkv_close(void *ctx)
{
	fmed_mkv *m = ctx;
	ffmkv_close(&m->mkv);
	ffmem_free(m);
}

static void mkv_meta(fmed_mkv *m, fmed_filt *d)
{
	ffstr name, val;
	if (m->mkv.tag == -1)
		return;
	ffstr_setz(&name, ffmmtag_str[m->mkv.tag]);
	ffstr_set2(&val, &m->mkv.tagval);
	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static const ushort mkv_codecs[] = {
	FFMKV_AUDIO_AAC, FFMKV_AUDIO_ALAC, FFMKV_AUDIO_MPEG, FFMKV_AUDIO_VORBIS,
};
static const char* const mkv_codecs_str[] = {
	"aac.decode", "alac.decode", "mpeg.decode", "vorbis.decode",
};

static int mkv_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_VORBIS_HDR, I_DATA, };
	fmed_mkv *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_set(&m->mkv.data, d->data, d->datalen);
		d->datalen = 0;
	}

again:
	switch (m->state) {
	case I_HDR:
		break;

	case I_VORBIS_HDR:
		r = ffmkv_vorbis_hdr(&m->mkv_vorbis);
		if (r < 0) {
			errlog(core, d->trk, NULL, "ffmkv_vorbis_hdr()");
			return FMED_RERR;
		} else if (r == 1) {
			m->state = I_DATA;
		} else {
			d->out = m->mkv_vorbis.out.ptr,  d->outlen = m->mkv_vorbis.out.len;
			return FMED_RDATA;
		}
		break;

	case I_DATA:
		break;
	}

	for (;;) {
		r = ffmkv_read(&m->mkv);
		switch (r) {
		case FFMKV_RMORE:
			if (d->flags & FMED_FLAST) {
				errlog(core, d->trk, NULL, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMKV_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFMKV_RDATA:
			goto data;

		case FFMKV_RHDR: {
			int i = ffint_find2(mkv_codecs, FFCNT(mkv_codecs), m->mkv.info.format);
			if (i == -1) {
				errlog(core, d->trk, NULL, "unsupported codec: %xu", m->mkv.info.format);
				return FMED_RERR;
			}

			const char *codec = mkv_codecs_str[i];
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = m->mkv.info.channels;
			d->audio.fmt.sample_rate = m->mkv.info.sample_rate;
			d->audio.total = m->mkv.info.total_samples;
			d->audio.bitrate = m->mkv.info.bitrate;

			if (m->mkv.info.format == FFMKV_AUDIO_VORBIS) {
				ffstr_set2(&m->mkv_vorbis.data, &m->mkv.info.asc);
				m->state = I_VORBIS_HDR;
				goto again;
			} else if (m->mkv.info.format == FFMKV_AUDIO_MPEG)
				m->mkv.info.asc.len = 0;

			d->out = m->mkv.info.asc.ptr,  d->outlen = m->mkv.info.asc.len;
			m->state = I_DATA;
			return FMED_RDATA;
		}

		case FFMKV_RTAG:
			mkv_meta(m, d);
			break;

		case FFMKV_RSEEK:
			// d->input.seek = ffmkv_seekoff(&m->mkv);
			return FMED_RMORE;

		case FFMKV_RWARN:
			warnlog(core, d->trk, NULL, "ffmkv_read(): %s", ffmkv_errstr(&m->mkv));
			break;

		case FFMKV_RERR:
		default:
			errlog(core, d->trk, NULL, "ffmkv_read(): %s", ffmkv_errstr(&m->mkv));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = ffmkv_cursample(&m->mkv);
	d->out = m->mkv.out.ptr,  d->outlen = m->mkv.out.len;
	dbglog(core, d->trk, NULL, "data size:%L", d->outlen);
	return FMED_RDATA;
}
