/** fmedia: .mp3 read
2021, Simon Zolin */

#include <avpack/mp3-read.h>

typedef struct mp3_in {
	mp3read mpg;
	ffstr in;
	void *trk;
	uint sample_rate;
	uint nframe;
	char codec_name[9];
	uint have_id32tag :1;
} mp3_in;

static void mp3_log(void *udata, const char *fmt, va_list va)
{
	mp3_in *m = udata;
	fmed_dbglogv(core, m->trk, NULL, fmt, va);
}

static void* mp3_open(fmed_track_info *d)
{
	if (d->stream_copy && 1 != d->track->cmd(d->trk, FMED_TRACK_META_HAVEUSER)) {

		if (NULL == (void*)d->track->cmd(d->trk, FMED_TRACK_FILT_ADD, "fmt.mp3-copy"))
			return NULL;
		return FMED_FILT_SKIP;
	}

	mp3_in *m = ffmem_new(mp3_in);
	m->trk = d->trk;
	ffuint64 total_size = 0;
	if ((int64)d->input.size != FMED_NULL) {
		total_size = d->input.size;
	}
	mp3read_open(&m->mpg, total_size);
	m->mpg.log = mp3_log;
	m->mpg.udata = m;
	m->mpg.id3v1.codepage = core->props->codepage;
	m->mpg.id3v2.codepage = core->props->codepage;
	return m;
}

static void mp3_close(void *ctx)
{
	mp3_in *m = ctx;
	mp3read_close(&m->mpg);
	ffmem_free(m);
}

static void mp3_meta(mp3_in *m, fmed_track_info *d, uint type)
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

static int mp3_process(void *ctx, fmed_track_info *d)
{
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

	ffstr out;
	for (;;) {

		if (d->seek_req && (int64)d->audio.seek != FMED_NULL && m->sample_rate != 0) {
			d->seek_req = 0;
			mp3read_seek(&m->mpg, ffpcm_samples(d->audio.seek, m->sample_rate));
			dbglog1(d->trk, "seek: %Ums", d->audio.seek);
		}

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
			ffs_format(m->codec_name, sizeof(m->codec_name), "MPEG1-L%u%Z", info->layer);
			d->audio.decoder = m->codec_name;
			d->datatype = "mpeg";
			d->mpeg1_delay = info->delay;
			d->mpeg1_padding = info->padding;
			d->mpeg1_vbr_scale = info->vbr_scale + 1;

			if (d->input_info)
				return FMED_RLASTOUT;

			if (!d->stream_copy
				&& 0 != d->track->cmd(d->trk, FMED_TRACK_ADDFILT, "mpeg.decode"))
				return FMED_RERR;

			break;
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
