/** fmedia: .ogg read
2021, Simon Zolin */

#include <avpack/ogg-read.h>

struct ogg_in_conf_t {
	byte seekable;
} conf;

static const fmed_conf_arg ogg_in_conf_args[] = {
	{ "seekable",  FMC_BOOL8,  FMC_O(struct ogg_in_conf_t, seekable) },
	{}
};

int ogg_in_conf(fmed_conf_ctx *ctx)
{
	conf.seekable = 1;
	fmed_conf_addctx(ctx, &conf, ogg_in_conf_args);
	return 0;
}

struct ogg_in {
	oggread og;
	ffstr in;
	void *trk;
	uint sample_rate;
	uint state;
	uint stmcopy :1;
};

static void ogg_log(void *udata, const char *fmt, va_list va)
{
	struct ogg_in *o = udata;
	fmed_dbglogv(core, o->trk, NULL, fmt, va);
}

static void* ogg_open(fmed_track_info *d)
{
	struct ogg_in *o = ffmem_new(struct ogg_in);
	o->trk = d->trk;

	ffuint64 total_size = 0;
	if (conf.seekable && (int64)d->input.size != FMED_NULL)
		total_size = d->input.size;
	oggread_open(&o->og, total_size);
	o->og.log = ogg_log;
	o->og.udata = o;

	if (d->stream_copy) {
		d->datatype = "OGG";
		o->stmcopy = 1;
	}
	return o;
}

static void ogg_close(void *ctx)
{
	struct ogg_in *o = ctx;
	oggread_close(&o->og);
	ffmem_free(o);
}

#define VORBIS_HEAD_STR  "\x01vorbis"
#define FLAC_HEAD_STR  "\x7f""FLAC"
#define OPUS_HEAD_STR  "OpusHead"

static int add_decoder(struct ogg_in *o, fmed_track_info *d, ffstr data)
{
	const char *dec;
	if (ffstr_matchz(&data, VORBIS_HEAD_STR))
		dec = "vorbis.decode";
	else if (ffstr_matchz(&data, OPUS_HEAD_STR))
		dec = "opus.decode";
	else if (ffstr_matchz(&data, FLAC_HEAD_STR)) {
		dec = "flac.ogg-in";
	} else {
		errlog1(d->trk, "Unknown codec in OGG packet: %*xb"
			, ffmin(data.len, 16), data.ptr);
		return 1;
	}
	if (0 != d->track->cmd(d->trk, FMED_TRACK_ADDFILT, (void*)dec)) {
		return 1;
	}
	return 0;
}

static uint file_bitrate(fmed_track_info *d, oggread *og, uint sample_rate)
{
	if (d->audio.total == 0 || og->total_size == 0)
		return 0;
	return ffpcm_brate(og->total_size, d->audio.total, sample_rate);
}

/*
. p0.1 -> vorbis-info (sample-rate)
. p1.x -> vorbis-tags
. px.x -> vorbis-data
*/
static int ogg_decode(void *ctx, fmed_track_info *d)
{
	enum { I_HDR, I_INFO, I_DATA, };
	struct ogg_in *o = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (o->state == I_INFO) {
		o->state = I_DATA;
		o->sample_rate = d->audio.fmt.sample_rate;
		d->audio.bitrate = file_bitrate(d, &o->og, o->sample_rate);
	}

	if (d->flags & FMED_FFWD) {
		o->in = d->data_in;
		d->data_in.len = 0;
	}

	for (;;) {

		if (d->seek_req && (int64)d->audio.seek != FMED_NULL && o->sample_rate != 0) {
			d->seek_req = 0;
			oggread_seek(&o->og, ffpcm_samples(d->audio.seek, o->sample_rate));
			dbglog1(d->trk, "seek: %Ums", d->audio.seek);
		}

		r = oggread_process(&o->og, &o->in, &d->data_out);
		switch (r) {
		case OGGREAD_MORE:
			if (d->flags & FMED_FLAST) {
				dbglog1(d->trk, "no eos page");
				d->outlen = 0;
				return FMED_RLASTOUT;
			}
			return FMED_RMORE;

		case OGGREAD_HEADER:
		case OGGREAD_DATA:
			if (o->state == I_HDR) {
				o->state = I_INFO;
				d->audio.total = oggread_info(&o->og)->total_samples;
				if (0 != add_decoder(o, d, d->data_out))
					return FMED_RERR;
			}
			goto data;

		case OGGREAD_DONE:
			d->data_out.len = 0;
			return FMED_RLASTOUT;

		case OGGREAD_SEEK:
			d->input.seek = oggread_offset(&o->og);
			return FMED_RMORE;

		case OGGREAD_ERROR:
		default:
			errlog1(d->trk, "oggread_process(): %s", oggread_error(&o->og));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = oggread_page_pos(&o->og);
	dbglog1(d->trk, "packet#%u.%u  length:%L  page-start-pos:%U"
		, (int)oggread_page_num(&o->og), (int)oggread_pkt_num(&o->og)
		, d->data_out.len
		, d->audio.pos);

	if (o->stmcopy) {
		int64 set_gpos = -1;
		const struct ogg_hdr *h = (struct ogg_hdr*)o->og.chunk.ptr;
		int page_is_last_pkt = (o->og.seg_off == h->nsegments);
		if (page_is_last_pkt) {
			d->ogg_flush = 1;
			set_gpos = o->og.page_endpos;
			fmed_setval("ogg_granpos", set_gpos);
			if (o->sample_rate != 0)
				set_gpos = ffpcm_time(set_gpos, o->sample_rate);
		}

		if (d->audio.until != FMED_NULL && d->audio.until > 0
			&& set_gpos > 0 && (uint64)set_gpos >= (uint64)d->audio.until) {

			dbglog1(d->trk, "reached time %Ums", d->audio.until);
			d->audio.until = FMED_NULL;
			return FMED_RLASTOUT;
		}
	}

	return FMED_RDATA;
}

const fmed_filter ogg_input = {
	ogg_open, ogg_decode, ogg_close
};
