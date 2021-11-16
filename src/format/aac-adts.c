/** AAC ADTS (.aac) reader.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>
#include <avpack/aac-read.h>

#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

extern const fmed_core *core;

struct aac {
	aacread adts;
	int64 seek_pos;
	uint64 pos;
	int sample_rate;
	int frno;
	ffstr in;
};

static void* aac_adts_open(fmed_filt *d)
{
	struct aac *a;
	if (NULL == (a = ffmem_new(struct aac)))
		return NULL;
	if (d->stream_copy) {
		const char *ofn = d->track->getvalstr(d->trk, "output");
		ffstr fn = FFSTR_INITZ(ofn), ext;
		ffstr_rsplitby(&fn, '.', NULL, &ext);
		if (ffstr_ieqz(&ext, "aac")) // return the whole adts frames only if the output is .aac file
			a->adts.options = AACREAD_WHOLEFRAME;
	}
	aacread_open(&a->adts);
	a->seek_pos = -1;
	return a;
}

static void aac_adts_close(void *ctx)
{
	struct aac *a = ctx;
	aacread_close(&a->adts);
	ffmem_free(a);
}

static int aac_adts_process(void *ctx, fmed_filt *d)
{
	struct aac *a = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		a->in = d->data_in;
		d->data_in.len = 0;
	}

	if ((int64)d->audio.seek != FMED_NULL && a->seek_pos == -1) {
		a->seek_pos = d->audio.seek;
		if (d->stream_copy)
			d->audio.seek = FMED_NULL;
		if ((uint64)a->seek_pos < a->pos) {
			a->pos = 0;
			d->input.seek = 0;
			return FMED_RMORE;
		}
	}

	ffstr out = {};

	for (;;) {
		r = aacread_process(&a->adts, &a->in, &out);

		switch (r) {

		case AACREAD_HEADER: {
			const struct aacread_info *info = aacread_info(&a->adts);
			d->audio.fmt.format = FFPCM_16;
			d->audio.fmt.sample_rate = info->sample_rate;
			a->sample_rate = info->sample_rate;
			d->audio.fmt.channels = info->channels;
			d->audio.total = 0;
			d->audio.decoder = "AAC";
			d->datatype = "aac";
			fmed_setval("audio_frame_samples", 1024);

			if (d->stream_copy) {
				d->audio.convfmt = d->audio.fmt;
			} else {
				if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "aac.decode"))
					return FMED_RERR;
			}

			d->data_out = out;
			return FMED_RDATA;
		}

		case AACREAD_DATA:
		case AACREAD_FRAME:
			a->pos += aacread_frame_samples(&a->adts);
			if (a->seek_pos != -1) {
				uint64 seek_samps = ffpcm_samples(a->seek_pos, a->sample_rate);
				if (a->pos < seek_samps)
					continue;
				a->seek_pos = -1;
			}

			goto data;

		case AACREAD_MORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RLASTOUT;
			}
			return FMED_RMORE;

		case AACREAD_WARN:
			warnlog1(d->trk, "aacread_process(): %s.  Offset: %U"
				, aacread_error(&a->adts), aacread_offset(&a->adts));
			continue;

		case AACREAD_ERROR:
			errlog1(d->trk, "aacread_process(): %s.  Offset: %U"
				, aacread_error(&a->adts), aacread_offset(&a->adts));
			return FMED_RERR;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = a->pos;
	dbglog1(d->trk, "passing frame #%u  samples:%u[%U]  size:%u"
		, a->frno++, aacread_frame_samples(&a->adts), d->audio.pos
		, out.len);
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter aac_adts_input = {
	aac_adts_open, aac_adts_process, aac_adts_close
};


struct aac_adts_out {
	uint state;
};

static void* aac_adts_out_open(fmed_filt *d)
{
	struct aac_adts_out *a;
	if (NULL == (a = ffmem_new(struct aac_adts_out)))
		return NULL;
	return a;
}

static void aac_adts_out_close(void *ctx)
{
	struct aac_adts_out *a = ctx;
	ffmem_free(a);
}

static int aac_adts_out_process(void *ctx, fmed_filt *d)
{
	struct aac_adts_out *a = ctx;

	switch (a->state) {
	case 0:
		if (!ffsz_eq(d->datatype, "aac")) {
			errlog1(d->trk, "unsupported data type: %s", d->datatype);
			return FMED_RERR;
		}
		// if (d->datalen != 0) {
		// skip ASC
		// }
		a->state = 1;
		return FMED_RMORE;
	case 1:
		break;
	}

	if (d->datalen == 0 && !(d->flags & FMED_FLAST))
		return FMED_RMORE;
	d->out = d->data,  d->outlen = d->datalen;
	d->datalen = 0;
	return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RDATA;
}

const fmed_filter aac_adts_output = {
	aac_adts_out_open, aac_adts_out_process, aac_adts_out_close
};
