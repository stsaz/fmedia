/** AAC ADTS (.aac) reader.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/aac-adts.h>


extern const fmed_core *core;

//INPUT
static void* aac_adts_open(fmed_filt *d);
static void aac_adts_close(void *ctx);
static int aac_adts_process(void *ctx, fmed_filt *d);
const fmed_filter aac_adts_input = {
	&aac_adts_open, &aac_adts_process, &aac_adts_close
};

//OUTPUT
static void* aac_adts_out_open(fmed_filt *d);
static void aac_adts_out_close(void *ctx);
static int aac_adts_out_process(void *ctx, fmed_filt *d);
const fmed_filter aac_adts_output = { &aac_adts_out_open, &aac_adts_out_process, &aac_adts_out_close };


struct aac {
	ffaac_adts adts;
	int64 seek_pos;
};

static void* aac_adts_open(fmed_filt *d)
{
	struct aac *a;
	if (NULL == (a = ffmem_new(struct aac)))
		return NULL;
	if (d->stream_copy)
		a->adts.options = FFAAC_ADTS_OPT_WHOLEFRAME;
	ffaac_adts_open(&a->adts);
	a->seek_pos = -1;
	return a;
}

static void aac_adts_close(void *ctx)
{
	struct aac *a = ctx;
	ffaac_adts_close(&a->adts);
	ffmem_free(a);
}

static int aac_adts_process(void *ctx, fmed_filt *d)
{
	struct aac *a = ctx;
	int r;
	ffstr blk;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		if (d->flags & FMED_FLAST)
			ffaac_adts_fin(&a->adts);
		ffaac_adts_input(&a->adts, d->data, d->datalen);
		d->datalen = 0;
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		a->seek_pos = d->audio.seek;
		if (d->stream_copy)
			d->audio.seek = FMED_NULL;
	}

	for (;;) {
		r = ffaac_adts_read(&a->adts);

		switch ((enum FFAAC_ADTS_R)r) {

		case FFAAC_ADTS_RHDR:
			d->audio.fmt.format = FFPCM_16;
			d->audio.fmt.sample_rate = a->adts.info.sample_rate;
			d->audio.fmt.channels = a->adts.info.channels;
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

			ffaac_adts_output(&a->adts, &blk);
			d->out = blk.ptr,  d->outlen = blk.len;
			return FMED_RDATA;

		case FFAAC_ADTS_RDATA:
		case FFAAC_ADTS_RFRAME:

			if (a->seek_pos != -1) {
				uint64 seek_samps = ffpcm_samples(a->seek_pos, a->adts.info.sample_rate);
				uint64 rpos = ffaac_adts_pos(&a->adts) + ffaac_adts_frsamples(&a->adts);
				if (rpos < seek_samps)
					continue;
				a->seek_pos = -1;
			}

			goto data;

		case FFAAC_ADTS_RMORE:
			return FMED_RMORE;

		case FFAAC_ADTS_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFAAC_ADTS_RWARN:
			warnlog(core, d->trk, "aac", "ffaac_adts_read(): %s.  Offset: %U"
				, ffaac_adts_errstr(&a->adts), ffaac_adts_off(&a->adts));
			continue;

		case FFAAC_ADTS_RERR:
			errlog(core, d->trk, "aac", "ffaac_adts_read(): %s.  Offset: %U"
				, ffaac_adts_errstr(&a->adts), ffaac_adts_off(&a->adts));
			return FMED_RERR;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}
	}

data:
	ffaac_adts_output(&a->adts, &blk);
	d->audio.pos = ffaac_adts_pos(&a->adts);
	dbglog(core, d->trk, NULL, "passing frame #%u  samples:%u[%U]  size:%u  off:%xU"
		, a->adts.frno, ffaac_adts_frsamples(&a->adts), d->audio.pos
		, blk.len, ffaac_adts_froffset(&a->adts));
	d->out = blk.ptr,  d->outlen = blk.len;
	return FMED_RDATA;
}


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
			fmed_errlog(core, d->trk, NULL, "unsupported data type: %s", d->datatype);
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
