/** MKV input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <avpack/mkv-read.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>


#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

extern const fmed_core *core;

struct mkvin {
	mkvread mkv;
	ffstr in;
	ffstr vorb_in;
	struct mkv_vorbis mkv_vorbis;
	void *trk;
	uint64 atrack;
	uint state;
	uint sample_rate;
	uint seeking :1;
};

void mkv_log(void *udata, ffstr msg)
{
	struct mkvin *m = udata;
	dbglog1(m->trk, "%S", &msg);
}

void* mkv_open(fmed_filt *d)
{
	struct mkvin *m = ffmem_new(struct mkvin);
	if (m == NULL) {
		errlog1(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}

	ffuint64 total_size = 0;
	if ((ffint64)d->input.size != FMED_NULL)
		total_size = d->input.size;
	mkvread_open(&m->mkv, total_size);
	m->mkv.log = mkv_log;
	m->mkv.udata = m;
	m->trk = d->trk;
	return m;
}

void mkv_close(void *ctx)
{
	struct mkvin *m = ctx;
	mkvread_close(&m->mkv);
	ffmem_free(m);
}

void mkv_meta(struct mkvin *m, fmed_filt *d)
{
	ffstr name, val;
	name = mkvread_tag(&m->mkv, &val);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

const ushort mkv_codecs[] = {
	MKV_A_AAC, MKV_A_ALAC, MKV_A_MPEG, MKV_A_VORBIS,
};
const char* const mkv_codecs_str[] = {
	"aac.decode", "alac.decode", "mpeg.decode", "vorbis.decode",
};
const ushort mkv_vcodecs[] = {
	MKV_V_AVC, MKV_V_HEVC,
};
const char* const mkv_vcodecs_str[] = {
	"H.264", "H.265",
};

void print_tracks(struct mkvin *m, fmed_filt *d)
{
	for (ffuint i = 0;  ;  i++) {
		const struct mkvread_audio_info *ai = mkvread_track_info(&m->mkv, i);
		if (ai == NULL)
			break;

		switch (ai->type) {
		case MKV_TRK_VIDEO: {
			const struct mkvread_video_info *vi = mkvread_track_info(&m->mkv, i);
			int i = ffint_find2(mkv_vcodecs, FF_COUNT(mkv_vcodecs), vi->codec);
			if (i != -1)
				d->video.decoder = mkv_vcodecs_str[i];

			dbglog1(d->trk, "track#%u: codec:%s  size:%ux%u"
				, i
				, d->video.decoder, vi->width, vi->height);
			d->video.width = vi->width;
			d->video.height = vi->height;
			break;
		}
		case MKV_TRK_AUDIO:
			dbglog1(d->trk, "track#%u: codec:%d  magic:%*xb  duration:%U  format:%u/%u"
				, i
				, ai->codec
				, ai->codec_conf.len, ai->codec_conf.ptr
				, ai->duration_msec
				, ai->sample_rate, ai->channels);
			break;
		}
	}
}

static const struct mkvread_audio_info* get_first_audio_track(struct mkvin *m)
{
	for (ffuint i = 0;  ;  i++) {
		const struct mkvread_audio_info *ai = mkvread_track_info(&m->mkv, i);
		if (ai == NULL)
			break;

		if (ai->type == MKV_TRK_AUDIO) {
			m->atrack = ai->id;
			return ai;
		}
	}
	return NULL;
}

int mkv_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_VORBIS_HDR, I_DATA, };
	struct mkvin *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->data_out.len = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_set(&m->in, d->data, d->datalen);
		d->datalen = 0;
	}

again:
	switch (m->state) {
	case I_HDR:
		break;

	case I_VORBIS_HDR:
		r = mkv_vorbis_hdr(&m->mkv_vorbis, &m->vorb_in, &d->data_out);
		if (r < 0) {
			errlog1(d->trk, "mkv_vorbis_hdr()");
			return FMED_RERR;
		} else if (r == 1) {
			m->state = I_DATA;
		} else {
			return FMED_RDATA;
		}
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
			m->seeking = 1;
			mkvread_seek(&m->mkv, d->audio.seek);
		}
		break;
	}

	for (;;) {
		r = mkvread_process(&m->mkv, &m->in, &d->data_out);
		switch (r) {
		case MKVREAD_MORE:
			if (d->flags & FMED_FLAST) {
				errlog1(d->trk, "file is incomplete");
				d->data_out.len = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case MKVREAD_DONE:
			d->data_out.len = 0;
			return FMED_RDONE;

		case MKVREAD_DATA:
			if (mkvread_block_trackid(&m->mkv) != m->atrack)
				continue;
			goto data;

		case MKVREAD_HEADER: {
			print_tracks(m, d);
			const struct mkvread_audio_info *ai = get_first_audio_track(m);
			if (ai == NULL) {
				errlog1(d->trk, "no audio track found");
				return FMED_RERR;
			}

			int i = ffint_find2(mkv_codecs, FF_COUNT(mkv_codecs), ai->codec);
			if (i == -1) {
				errlog1(d->trk, "unsupported codec: %xu", ai->codec);
				return FMED_RERR;
			}

			const char *codec = mkv_codecs_str[i];
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = ai->channels;
			d->audio.fmt.sample_rate = ai->sample_rate;
			m->sample_rate = ai->sample_rate;
			d->audio.total = ffpcm_samples(ai->duration_msec, ai->sample_rate);
			// d->audio.bitrate = ;

			if ((int64)d->audio.seek != FMED_NULL) {
				m->seeking = 1;
				mkvread_seek(&m->mkv, d->audio.seek);
			}

			if (ai->codec == MKV_A_VORBIS) {
				ffstr_set2(&m->vorb_in, &ai->codec_conf);
				m->state = I_VORBIS_HDR;
				goto again;
			} else if (ai->codec == MKV_A_MPEG) {
				//
			} else
				d->data_out = ai->codec_conf;

			m->state = I_DATA;
			return FMED_RDATA;
		}

		case MKVREAD_TAG:
			mkv_meta(m, d);
			break;

		case MKVREAD_SEEK:
			d->input.seek = mkvread_offset(&m->mkv);
			return FMED_RMORE;

		case MKVREAD_ERROR:
		default:
			errlog1(d->trk, "mkvread_read(): %s  offset:%xU"
				, mkvread_error(&m->mkv), mkvread_offset(&m->mkv));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = ffpcm_samples(mkvread_curpos(&m->mkv), m->sample_rate);
	dbglog1(d->trk, "data size:%L  pos:%U", d->data_out.len, d->audio.pos);
	m->seeking = 0;
	return FMED_RDATA;
}

const fmed_filter mkv_input = {
	mkv_open, mkv_process, mkv_close
};
