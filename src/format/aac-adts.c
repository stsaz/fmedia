/** AAC ADTS (.aac) reader.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/aac-adts.h>


const fmed_core *core;

//INPUT
static void* aac_adts_open(fmed_filt *d);
static void aac_adts_close(void *ctx);
static int aac_adts_process(void *ctx, fmed_filt *d);
const fmed_filter aac_adts_input = {
	&aac_adts_open, &aac_adts_process, &aac_adts_close
};

struct aac {
	ffaac_adts adts;
};


static void* aac_adts_open(fmed_filt *d)
{
	struct aac *a;
	if (NULL == (a = ffmem_new(struct aac)))
		return NULL;
	ffaac_adts_open(&a->adts);
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
