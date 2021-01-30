/** MP4 input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <avpack/mp4read.h>
#include <FF/mtags/mmtag.h>


#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)


static const fmed_core *core;
static const fmed_queue *qu;

#include <format/mp4-write.h>

typedef struct mp4 {
	mp4read mp;
	ffstr in;
	void *trk;
	uint srate;
	uint state;
	uint seeking :1;
} mp4;

static void mp4_meta(mp4 *m, fmed_filt *d);

//FMEDIA MODULE
static const void* mp4_iface(const char *name);
static int mp4_sig(uint signo);
static void mp4_destroy(void);
static const fmed_mod fmed_mp4_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mp4_iface, &mp4_sig, &mp4_destroy
};

//INPUT
static void* mp4_in_create(fmed_filt *d);
static void mp4_in_free(void *ctx);
static int mp4_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mp4_input = {
	&mp4_in_create, &mp4_in_decode, &mp4_in_free
};

//OUTPUT
static void* mp4_out_create(fmed_filt *d);
static void mp4_out_free(void *ctx);
static int mp4_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter mp4_output = {
	&mp4_out_create, &mp4_out_encode, &mp4_out_free
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mp4_mod;
}


static const void* mp4_iface(const char *name)
{
	if (!ffsz_cmp(name, "input"))
		return &fmed_mp4_input;
	else if (!ffsz_cmp(name, "output"))
		return &mp4_output;
	return NULL;
}

static int mp4_sig(uint signo)
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

static void mp4_destroy(void)
{
}

static void mp4_log(void *udata, ffstr msg)
{
	mp4 *m = udata;
	dbglog1(m->trk, "%S", &msg);
}

static void* mp4_in_create(fmed_filt *d)
{
	mp4 *m = ffmem_tcalloc1(mp4);
	if (m == NULL)
		return NULL;
	m->trk = d->trk;

	mp4read_open(&m->mp);
	m->mp.log = mp4_log;
	m->mp.udata = m;

	if ((int64)d->input.size != FMED_NULL)
		m->mp.total_size = d->input.size;

	d->datatype = "mp4";
	return m;
}

static void mp4_in_free(void *ctx)
{
	mp4 *m = ctx;
	mp4read_close(&m->mp);
	ffmem_free(m);
}

static void mp4_meta(mp4 *m, fmed_filt *d)
{
	ffstr name, val;
	if (m->mp.tag == 0)
		return;
	ffstr_setz(&name, ffmmtag_str[m->mp.tag]);
	val = m->mp.tagval;

	dbglog1(d->trk, "tag: %S: %S", &name, &val);

	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static void print_tracks(mp4 *m, fmed_filt *d)
{
	for (ffuint i = 0;  ;  i++) {
		const struct mp4read_audio_info *ai = mp4read_track_info(&m->mp, i);
		if (ai == NULL)
			break;
		switch (ai->type) {
		case 0: {
			const struct mp4read_video_info *vi = mp4read_track_info(&m->mp, i);
			dbglog1(d->trk, "track#%u: codec:%s  size:%ux%u"
				, i
				, vi->codec_name, vi->width, vi->height);
			d->video.decoder = vi->codec_name;
			d->video.width = vi->width;
			d->video.height = vi->height;
			break;
		}
		case 1:
			dbglog1(d->trk, "track#%u: codec:%s  magic:%*xb  total_samples:%u  format:%u/%u"
				, i
				, ai->codec_name
				, ai->codec_conf.len, ai->codec_conf.ptr
				, ai->total_samples
				, ai->format.rate, ai->format.channels);
			break;
		}
	}
}

static const struct mp4read_audio_info* get_last_audio_track(mp4 *m)
{
	for (ffuint i = 0;  ;  i++) {
		const struct mp4read_audio_info *ai = mp4read_track_info(&m->mp, i);
		if (ai == NULL)
			break;
		if (ai->type == 1) {
			mp4read_track_activate(&m->mp, i);
			return ai;
		}
	}
	return NULL;
}

/**
. Read .mp4 data.
. Add the appropriate audio decoding filter.
  Set decoder properties.
  The first output data block contains codec-specific configuration data.
. The subsequent blocks are audio frames.
*/
static int mp4_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA1, I_DATA, };
	mp4 *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	m->in = d->data_in;

	ffstr out;

	for (;;) {

	switch (m->state) {

	case I_DATA1:
	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
			m->seeking = 1;
			uint64 seek = ffpcm_samples(d->audio.seek, m->srate);
			mp4read_seek(&m->mp, seek);
			if (d->stream_copy)
				d->audio.seek = FMED_NULL;
		}
		if (m->state == I_DATA1) {
			m->state = I_DATA;
			return FMED_RDATA;
		}
		//fallthrough

	case I_HDR:
		r = mp4read_process(&m->mp, &m->in, &out);
		switch (r) {
		case MP4READ_MORE:
			if (d->flags & FMED_FLAST) {
				warnlog1(d->trk, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case MP4READ_HEADER: {
			print_tracks(m, d);
			const struct mp4read_audio_info *ai = get_last_audio_track(m);
			if (ai == NULL) {
				errlog1(d->trk, "no audio track found");
				return FMED_RERR;
			}
			if (ai->format.bits == 0) {
				errlog1(d->trk, "mp4read_process(): %s", mp4read_error(&m->mp));
				return FMED_RERR;
			}
			ffpcm_set((ffpcm*)&d->audio.fmt, ai->format.bits, ai->format.channels, ai->format.rate);
			d->audio.total = ai->total_samples;

			const char *filt;
			switch (ai->codec) {
			case FFMP4_ALAC:
				filt = "alac.decode";
				d->audio.bitrate = ai->real_bitrate;
				break;

			case FFMP4_AAC:
				filt = "aac.decode";
				if (!d->stream_copy) {
					fmed_setval("audio_enc_delay", ai->enc_delay);
					fmed_setval("audio_end_padding", ai->end_padding);
					d->audio.bitrate = (ai->aac_bitrate != 0) ? ai->aac_bitrate : ai->real_bitrate;
				}
				break;

			case FFMP4_MPEG1:
				filt = "mpeg.decode";
				d->audio.bitrate = (ai->aac_bitrate != 0) ? ai->aac_bitrate : 0;
				break;

			default:
				errlog1(d->trk, "%s: decoding unsupported", ai->codec_name);
				return FMED_RERR;
			}

			if (ai->frame_samples != 0)
				fmed_setval("audio_frame_samples", ai->frame_samples);

			if (!d->stream_copy
				&& 0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)filt))
				return FMED_RERR;

			m->srate = ai->format.rate;

			d->data_in = m->in;
			d->data_out = ai->codec_conf;
			m->state = I_DATA1;

			if (d->input_info)
				return FMED_ROK;
			continue;
		}

		case MP4READ_TAG:
			mp4_meta(m, d);
			break;

		case MP4READ_DATA:
			d->audio.pos = mp4read_cursample(&m->mp);
			dbglog1(d->trk, "passing %L bytes at position #%U"
				, out.len, d->audio.pos);
			d->data_in = m->in;
			d->data_out = out;
			m->seeking = 0;
			return FMED_RDATA;

		case MP4READ_DONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case MP4READ_SEEK:
			d->input.seek = m->mp.off;
			return FMED_RMORE;

		case MP4READ_WARN:
			warnlog1(d->trk, "mp4read_process(): at offset 0x%xU: %s"
				, m->mp.off, mp4read_error(&m->mp));
			break;

		case MP4READ_ERROR:
			errlog1(d->trk, "mp4read_process(): %s", mp4read_error(&m->mp));
			return FMED_RERR;
		}
		break;
	}

	}

	//unreachable
}
