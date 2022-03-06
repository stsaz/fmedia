/** MKV input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <avpack/mkv-read.h>
#include <format/mmtag.h>


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
	MKV_A_AAC, MKV_A_ALAC, MKV_A_MPEG, MKV_A_OPUS, MKV_A_VORBIS, MKV_A_PCM,
};
const char* const mkv_codecs_str[] = {
	"aac.decode", "alac.decode", "mpeg.decode", "opus.decode", "vorbis.decode", "",
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
			int i = ffarrint16_find(mkv_vcodecs, FF_COUNT(mkv_vcodecs), vi->codec);
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
		if (m->seeking && (int64)d->audio.seek == FMED_NULL) {
			m->seeking = 0;
		}
		break;
	}

	ffstr out;
	for (;;) {
		r = mkvread_process(&m->mkv, &m->in, &out);
		switch (r) {
		case MKVREAD_MORE:
			if (d->flags & FMED_FLAST) {
				dbglog1(d->trk, "file is incomplete");
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case MKVREAD_DONE:
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

			if (d->audio.abs_seek != 0) {
				d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "plist.cuehook");
				m->seeking = 1;
				uint64 msec = d->audio.abs_seek;
				if (d->audio.abs_seek < 0)
					msec = -d->audio.abs_seek * 1000 / 75;
				if ((int64)d->audio.seek != FMED_NULL)
					msec += d->audio.seek;
				mkvread_seek(&m->mkv, msec);
			}

			int i = ffarrint16_find(mkv_codecs, FF_COUNT(mkv_codecs), ai->codec);
			if (i == -1) {
				errlog1(d->trk, "unsupported codec: %xu", ai->codec);
				return FMED_RERR;
			}

			const char *codec = mkv_codecs_str[i];
			if (codec[0] == '\0') {
				d->datatype = "pcm";
				d->audio.fmt.format = ai->bits;
				d->audio.fmt.ileaved = 1;

			} else if (!d->stream_copy) {
				if (NULL == (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, (void*)codec)) {
					return FMED_RERR;
				}
			}
			d->audio.fmt.channels = ai->channels;
			d->audio.fmt.sample_rate = ai->sample_rate;
			m->sample_rate = ai->sample_rate;
			d->audio.total = ffpcm_samples(ai->duration_msec, ai->sample_rate);
			// d->audio.bitrate = ;

			if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
				m->seeking = 1;
				mkvread_seek(&m->mkv, d->audio.seek);
			}

			if (ai->codec == MKV_A_VORBIS) {
				ffstr_set2(&m->vorb_in, &ai->codec_conf);
				m->state = I_VORBIS_HDR;
				goto again;
			} else if (ai->codec == MKV_A_MPEG) {
				//
			} else if (ai->codec == MKV_A_OPUS) {
				if (d->stream_copy) {
					d->datatype = "Opus";
					d->audio.fmt.format = FFPCM_FLOAT;
					fmed_setval("opus_no_tags", 1);
				}
				d->data_out = ai->codec_conf;
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
	dbglog1(d->trk, "data size:%L  pos:%U", out.len, d->audio.pos);
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter mkv_input = {
	mkv_open, mkv_process, mkv_close
};
