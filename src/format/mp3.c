/** MPEG Layer3 (.mp3) reader/writer.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <avpack/mp3-read.h>
#include <FF/audio/pcm.h>
#include <format/mmtag.h>
#include <FF/array.h>

#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

extern const fmed_core *core;
extern const fmed_queue *qu;

#include <format/mp3-write.h>
#include <format/mp3-copy.h>

typedef struct mp3_in {
	mp3read mpg;
	ffstr in;
	uint sample_rate;
	uint state;
	uint nframe;
	uint have_id32tag :1
		, seeking :1
		;
} mp3_in;

void* mp3_open(fmed_filt *d)
{
	if (d->stream_copy && !d->track->cmd(d->trk, FMED_TRACK_META_HAVEUSER)) {

		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "fmt.mp3-copy"))
			return NULL;
		return FMED_FILT_SKIP;
	}

	mp3_in *m = ffmem_new(mp3_in);
	if (m == NULL)
		return NULL;
	ffuint64 total_size = 0;
	if ((int64)d->input.size != FMED_NULL) {
		total_size = d->input.size;
	}
	mp3read_open(&m->mpg, total_size);
	m->mpg.id3v2.codepage = core->getval("codepage");
	return m;
}

void mp3_close(void *ctx)
{
	mp3_in *m = ctx;
	mp3read_close(&m->mpg);
	ffmem_free(m);
}

void mp3_meta(mp3_in *m, fmed_filt *d, uint type)
{
	if (type == MP3READ_ID32) {
		if (!m->have_id32tag) {
			m->have_id32tag = 1;
			dbglog1(d->trk, "ID3v2.%u  size:%u"
				, id3v2read_version(&m->mpg.id3v2), id3v2read_size(&m->mpg.id3v2));
		}
	}

	ffstr name, val;
	int tag = mp3read_tag(&m->mpg, &name, &val);
	if (tag != 0)
		ffstr_setz(&name, ffmmtag_str[tag]);

	dbglog1(d->trk, "tag: %S: %S", &name, &val);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

int mp3_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	mp3_in *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->datalen != 0) {
		m->in = d->data_in;
		d->datalen = 0;
	}

again:
	switch (m->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
			m->seeking = 1;
			mp3read_seek(&m->mpg, ffpcm_samples(d->audio.seek, m->sample_rate));
			if (d->stream_copy)
				d->audio.seek = FMED_NULL;
		}
		break;
	}

	ffstr out;
	for (;;) {
		r = mp3read_process(&m->mpg, &m->in, &out);

		switch (r) {
		case MPEG1READ_DATA:
			goto data;

		case MPEG1READ_MORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case MP3READ_DONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case MPEG1READ_HEADER: {
			const struct mpeg1read_info *info = mp3read_info(&m->mpg);
			d->audio.fmt.format = FFPCM_16;
			m->sample_rate = info->sample_rate;
			d->audio.fmt.sample_rate = info->sample_rate;
			d->audio.fmt.channels = info->channels;
			d->audio.bitrate = info->bitrate;
			d->audio.total = info->total_samples;
			d->audio.decoder = "MPEG";
			d->datatype = "mpeg";
			fmed_setval("mpeg_delay", info->delay);

			if (d->input_info)
				return FMED_RDONE;

			m->state = I_DATA;

			if (d->audio.abs_seek != 0) {
				d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "plist.cuehook");
				m->seeking = 1;
				uint64 samples = fmed_apos_samples(d->audio.abs_seek, m->sample_rate);
				mp3read_seek(&m->mpg, samples);
			}

			if (!d->stream_copy
				&& 0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpeg.decode"))
				return FMED_RERR;

			if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
				m->seeking = 1;
				mp3read_seek(&m->mpg, ffpcm_samples(d->audio.seek, m->sample_rate));
			}

			goto again;
		}

		case MP3READ_ID31:
		case MP3READ_ID32:
		case MP3READ_APETAG:
			mp3_meta(m, d, r);
			break;

		case MPEG1READ_SEEK:
			d->input.seek = mp3read_offset(&m->mpg);
			return FMED_RMORE;

		case MP3READ_WARN:
			warnlog1(d->trk, "mp3read_read(): %s. Near sample %U, offset %U"
				, mp3read_error(&m->mpg), mp3read_cursample(&m->mpg), mp3read_offset(&m->mpg));
			break;

		case MPEG1READ_ERROR:
		default:
			errlog1(d->trk, "mp3read_read(): %s. Near sample %U, offset %U"
				, mp3read_error(&m->mpg), mp3read_cursample(&m->mpg), mp3read_offset(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	if (m->seeking)
		m->seeking = 0;
	d->audio.pos = mp3read_cursample(&m->mpg);
	dbglog1(d->trk, "passing frame #%u  samples:%u[%U]  size:%u  br:%u  off:%xU"
		, ++m->nframe, mpeg1_samples(out.ptr), d->audio.pos, (uint)out.len
		, mpeg1_bitrate(out.ptr), (ffint64)mp3read_offset(&m->mpg) - out.len);
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter mp3_input = {
	mp3_open, mp3_process, mp3_close
};
