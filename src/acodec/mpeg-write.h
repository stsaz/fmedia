/** fmedia: MPEG write
2015, Simon Zolin */

#include <acodec/alib3-bridge/mp3lame.h>

typedef struct mpeg_enc {
	uint state;
	ffmpg_enc mpg;
} mpeg_enc;

struct mpeg_enc_conf_t {
	uint qual;
};
struct mpeg_enc_conf_t mpeg_enc_conf;

const fmed_conf_arg mpeg_enc_conf_args[] = {
	{ "quality",	FMC_INT32,  FMC_O(struct mpeg_enc_conf_t, qual) },
	{}
};

int mpeg_enc_config(fmed_conf_ctx *ctx)
{
	mpeg_enc_conf.qual = 2;
	fmed_conf_addctx(ctx, &mpeg_enc_conf, mpeg_enc_conf_args);
	return 0;
}

void* mpeg_enc_open(fmed_filt *d)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
		return NULL;
	}

	mpeg_enc *m = ffmem_new(mpeg_enc);
	if (m == NULL)
		return NULL;

	d->mpg_lametag = 0;
	return m;
}

void mpeg_enc_close(void *ctx)
{
	mpeg_enc *m = ctx;
	ffmpg_enc_close(&m->mpg);
	ffmem_free(m);
}

int mpeg_enc_process(void *ctx, fmed_filt *d)
{
	mpeg_enc *m = ctx;
	ffpcm pcm;
	int r, qual;

	switch (m->state) {
	case 0:
	case 1:
		ffpcm_fmtcopy(&pcm, &d->audio.convfmt);
		m->mpg.ileaved = d->audio.convfmt.ileaved;

		qual = (d->mpeg.quality != -1) ? d->mpeg.quality : (int)mpeg_enc_conf.qual;
		if (0 != (r = ffmpg_create(&m->mpg, &pcm, qual))) {

			if (r == FFMPG_EFMT && m->state == 0) {
				d->audio.convfmt.format = pcm.format;
				m->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, "mpeg", "ffmpg_create() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}

		if ((int64)d->audio.total != FMED_NULL) {
			uint64 total = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;
			d->output.size = ffmpg_enc_size(&m->mpg, total);
		}
		d->datatype = "mpeg";

		m->state = 2;
		// break

	case 2:
		m->mpg.pcm = (void*)d->data;
		m->mpg.pcmlen = d->datalen;
		m->state = 3;
		// break

	case 3:
		break;
	}

	for (;;) {
		r = ffmpg_encode(&m->mpg);
		switch (r) {

		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (!(d->flags & FMED_FLAST)) {
				m->state = 2;
				return FMED_RMORE;
			}
			m->mpg.fin = 1;
			break;

		case FFMPG_RDONE:
			d->mpg_lametag = 1;
			goto data;

		default:
			errlog(core, d->trk, "mpeg", "ffmpg_encode() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	d->out = m->mpg.data;
	d->outlen = m->mpg.datalen;

	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, m->mpg.datalen);
	return (r == FFMPG_RDONE) ? FMED_RDONE : FMED_RDATA;
}

const fmed_filter fmed_mpeg_enc = {
	mpeg_enc_open, mpeg_enc_process, mpeg_enc_close
};
