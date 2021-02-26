/** AVI input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <avpack/avi-read.h>
#include <FF/mtags/mmtag.h>

extern const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

typedef struct fmed_avi {
	aviread avi;
	void *trk;
	ffstr in;
	uint state;
} fmed_avi;

void avi_log(void *udata, ffstr msg)
{
	fmed_avi *a = udata;
	dbglog1(a->trk, "%S", &msg);
}

static void* avi_open(fmed_filt *d)
{
	fmed_avi *a = ffmem_tcalloc1(fmed_avi);
	if (a == NULL) {
		errlog1(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}
	a->trk = d->trk;
	aviread_open(&a->avi);
	a->avi.log = avi_log;
	a->avi.udata = a;
	return a;
}

static void avi_close(void *ctx)
{
	fmed_avi *a = ctx;
	aviread_close(&a->avi);
	ffmem_free(a);
}

static void avi_meta(fmed_avi *a, fmed_filt *d)
{
	ffstr name, val;
	int tag = aviread_tag(&a->avi, &val);
	if (tag == -1)
		return;
	ffstr_setz(&name, ffmmtag_str[tag]);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static const ushort avi_codecs[] = {
	AVI_A_AAC, AVI_A_MP3,
};
static const char* const avi_codecs_str[] = {
	"aac.decode", "mpeg.decode",
};

static const struct avi_audio_info* get_first_audio_track(struct fmed_avi *a)
{
	for (ffuint i = 0;  ;  i++) {
		const struct avi_audio_info *ai = aviread_track_info(&a->avi, i);
		if (ai == NULL)
			break;

		if (ai->type == 1) {
			aviread_track_activate(&a->avi, i);
			return ai;
		}
	}
	return NULL;
}

static int avi_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_avi *a = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		a->in = d->data_in;
		d->data_in.len = 0;
	}

	switch (a->state) {
	case I_HDR:
		break;

	case I_DATA:
		break;
	}

	for (;;) {
		r = aviread_process(&a->avi, &a->in, &d->data_out);
		switch (r) {
		case AVIREAD_MORE:
			if (d->flags & FMED_FLAST) {
				errlog1(d->trk, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case AVIREAD_DONE:
			d->data_out.len = 0;
			return FMED_RDONE;

		case AVIREAD_DATA:
			goto data;

		case AVIREAD_HEADER: {
			const struct avi_audio_info *ai = get_first_audio_track(a);
			int i = ffint_find2(avi_codecs, FFCNT(avi_codecs), ai->codec);
			if (i == -1) {
				errlog1(d->trk, "unsupported codec: %xu", ai->codec);
				return FMED_RERR;
			}

			const char *codec = avi_codecs_str[i];
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = ai->channels;
			d->audio.fmt.sample_rate = ai->sample_rate;
			d->audio.total = ffpcm_samples(ai->duration_msec, ai->sample_rate);
			d->audio.bitrate = ai->bitrate;

			d->data_out = ai->codec_conf;
			a->state = I_DATA;
			return FMED_RDATA;
		}

		case AVIREAD_TAG:
			avi_meta(a, d);
			break;

		// case AVIREAD_SEEK:
			// d->input.seek = aviread_offset(&a->avi);
			// return FMED_RMORE;

		case AVIREAD_ERROR:
		default:
			errlog1(d->trk, "aviread_process(): %s", aviread_error(&a->avi));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = aviread_cursample(&a->avi);
	return FMED_RDATA;
}

const fmed_filter avi_input = {
	avi_open, avi_process, avi_close
};
