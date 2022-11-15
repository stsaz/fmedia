/** fmedia: .mp4 read
2021, Simon Zolin */

#include <avpack/mp4-read.h>

typedef struct mp4 {
	mp4read mp;
	ffstr in;
	void *trk;
	uint srate;
	uint state;
} mp4;

static void mp4_log(void *udata, const char *fmt, va_list va)
{
	mp4 *m = udata;
	fmed_dbglogv(core, m->trk, NULL, fmt, va);
}

static void* mp4_in_create(fmed_track_info *d)
{
	mp4 *m = ffmem_tcalloc1(mp4);
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

static void mp4_meta(mp4 *m, fmed_track_info *d)
{
	ffstr name, val;
	int tag = mp4read_tag(&m->mp, &val);
	if (tag == 0)
		return;
	ffstr_setz(&name, ffmmtag_str[tag]);

	dbglog1(d->trk, "tag: %S: %S", &name, &val);

	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static void print_tracks(mp4 *m, fmed_track_info *d)
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

static const struct mp4read_audio_info* get_first_audio_track(mp4 *m)
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
static int mp4_in_decode(void *ctx, fmed_track_info *d)
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
		if (d->seek_req && (int64)d->audio.seek != FMED_NULL) {
			d->seek_req = 0;
			uint64 seek = ffpcm_samples(d->audio.seek, m->srate);
			mp4read_seek(&m->mp, seek);
			dbglog1(d->trk, "seek: %Ums", d->audio.seek);
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
			const struct mp4read_audio_info *ai = get_first_audio_track(m);
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
			case MP4_A_ALAC:
				filt = "alac.decode";
				d->audio.bitrate = ai->real_bitrate;
				break;

			case MP4_A_AAC:
				filt = "aac.decode";
				if (!d->stream_copy) {
					d->a_enc_delay = ai->enc_delay;
					d->a_end_padding = ai->end_padding;
					d->audio.bitrate = (ai->aac_bitrate != 0) ? ai->aac_bitrate : ai->real_bitrate;
				}
				break;

			case MP4_A_MPEG1:
				filt = "mpeg.decode";
				d->audio.bitrate = (ai->aac_bitrate != 0) ? ai->aac_bitrate : 0;
				break;

			default:
				errlog1(d->trk, "%s: decoding unsupported", ai->codec_name);
				return FMED_RERR;
			}

			if (ai->frame_samples != 0)
				d->a_frame_samples = ai->frame_samples;

			if (!d->stream_copy
				&& 0 != d->track->cmd(d->trk, FMED_TRACK_ADDFILT, (void*)filt))
				return FMED_RERR;

			m->srate = ai->format.rate;

			d->data_in = m->in;
			d->data_out = ai->codec_conf;
			m->state = I_DATA1;

			if (d->input_info)
				return FMED_RLASTOUT;
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

const fmed_filter mp4_input = {
	mp4_in_create, mp4_in_decode, mp4_in_free
};
