/** CAF input.
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>
#include <avpack/caf-read.h>
#include <FF/audio/pcm.h>

#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)

extern const fmed_core *core;

typedef struct fmed_caf {
	cafread caf;
	void *trk;
	ffstr in;
	uint state;
} fmed_caf;

void caf_log(void *udata, ffstr msg)
{
	fmed_caf *c = udata;
	dbglog1(c->trk, "%S", &msg);
}

static void* caf_open(fmed_filt *d)
{
	fmed_caf *c = ffmem_tcalloc1(fmed_caf);
	if (c == NULL) {
		errlog1(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}
	c->trk = d->trk;
	cafread_open(&c->caf);
	c->caf.log = caf_log;
	c->caf.udata = c;
	return c;
}

static void caf_close(void *ctx)
{
	fmed_caf *c = ctx;
	cafread_close(&c->caf);
	ffmem_free(c);
}

static void caf_meta(fmed_caf *c, fmed_filt *d)
{
	c->caf.tagname = cafread_tag(&c->caf, &c->caf.tagval);
	d->track->meta_set(d->trk, &c->caf.tagname, &c->caf.tagval, FMED_QUE_TMETA);
}

static const byte caf_codecs[] = {
	CAF_AAC, CAF_ALAC, CAF_LPCM,
};
static const char* const caf_codecs_str[] = {
	"aac.decode", "alac.decode", "",
};

static int caf_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_caf *c = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->data_out.len = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		c->in = d->data_in;
		d->data_in.len = 0;
	}

	switch (c->state) {
	case I_HDR:
		break;

	case I_DATA:
		break;
	}

	for (;;) {
		r = cafread_process(&c->caf, &c->in, &d->data_out);
		switch (r) {
		case CAFREAD_MORE_OR_DONE:
			if (d->flags & FMED_FLAST) {
				d->data_out.len = 0;
				return FMED_RDONE;
			}
			// fallthrough

		case CAFREAD_MORE:
			if (d->flags & FMED_FLAST) {
				errlog1(d->trk, "file is incomplete");
				d->data_out.len = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case CAFREAD_DONE:
			d->data_out.len = 0;
			return FMED_RDONE;

		case CAFREAD_DATA:
			goto data;

		case CAFREAD_HEADER: {
			const caf_info *ai = cafread_info(&c->caf);
			dbglog1(d->trk, "packets:%U  frames/packet:%u  bytes/packet:%u"
				, ai->total_packets, ai->packet_frames, ai->packet_bytes);

			int i = ffint_find1(caf_codecs, FFCNT(caf_codecs), ai->codec);
			if (i == -1) {
				errlog1(d->trk, "unsupported codec: %xu", ai->codec);
				return FMED_RERR;
			}

			const char *codec = caf_codecs_str[i];
			if (codec[0] == '\0') {
				if (ai->format & CAF_FMT_FLOAT) {
					errlog1(d->trk, "float data isn't supported");
					return FMED_RERR;
				}
				if (!(ai->format & CAF_FMT_LE)) {
					errlog1(d->trk, "big-endian data isn't supported");
					return FMED_RERR;
				}
				d->datatype = "pcm";
				d->audio.fmt.format = ai->format & 0xff;
				d->audio.fmt.ileaved = 1;
			} else if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = ai->channels;
			d->audio.fmt.sample_rate = ai->sample_rate;
			d->audio.total = ai->total_frames;
			d->audio.bitrate = ai->bitrate;

			if (d->input_info)
				return FMED_RDONE;

			d->data_out = ai->codec_conf;
			c->state = I_DATA;
			return FMED_RDATA;
		}

		case CAFREAD_TAG:
			caf_meta(c, d);
			break;

		case CAFREAD_SEEK:
			d->input.seek = cafread_offset(&c->caf);
			return FMED_RMORE;

		case CAFREAD_ERROR:
		default:
			errlog1(d->trk, "cafread_process(): %s", cafread_error(&c->caf));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = cafread_cursample(&c->caf);
	return FMED_RDATA;
}

const fmed_filter caf_input = {
	caf_open, caf_process, caf_close
};
