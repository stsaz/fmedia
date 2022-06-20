/** FLAC-OGG input.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>
#include <avpack/flac-ogg-read.h>


#undef dbglog
#undef errlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)


extern const fmed_core *core;

static void* flacogg_in_create(fmed_filt *d);
static void flacogg_in_free(void *ctx);
static int flacogg_in_read(void *ctx, fmed_filt *d);
const fmed_filter fmed_flacogg_input = {
	&flacogg_in_create, &flacogg_in_read, &flacogg_in_free
};


struct flacogg_in {
	flacoggread fo;
	uint64 apos, page_start_pos;
	ffstr in;
	uint fr_samples;
	uint sample_rate;
	uint64 seek_ms;
};

static void* flacogg_in_create(fmed_filt *d)
{
	struct flacogg_in *f = ffmem_new(struct flacogg_in);
	f->seek_ms = (uint64)-1;
	flacoggread_open(&f->fo);
	return f;
}

static void flacogg_in_free(void *ctx)
{
	struct flacogg_in *f = ctx;
	flacoggread_close(&f->fo);
	ffmem_free(f);
}

static void flacogg_meta(struct flacogg_in *f, fmed_filt *d)
{
	ffstr name, val;
	int tag = flacoggread_tag(&f->fo, &name, &val);
	if (tag != 0)
		ffstr_setz(&name, ffmmtag_str[tag]);
	dbglog(d->trk, "%S: %S", &name, &val);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static int flacogg_in_read(void *ctx, fmed_filt *d)
{
	struct flacogg_in *f = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_set(&f->in, d->data, d->datalen);
		d->datalen = 0;
		if (f->page_start_pos != d->audio.pos) {
			f->apos = d->audio.pos;
			f->page_start_pos = d->audio.pos;
		}
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		f->seek_ms = d->audio.seek;
		d->audio.seek = FMED_NULL;
	}

	ffstr out = {};

	for (;;) {
		r = flacoggread_process(&f->fo, &f->in, &out);

		switch (r) {
		case FLACOGGREAD_HEADER: {
			d->audio.decoder = "FLAC";
			const struct flac_info *info = flacoggread_info(&f->fo);
			d->audio.fmt.format = info->bits;
			d->audio.fmt.channels = info->channels;
			d->audio.fmt.sample_rate = info->sample_rate;
			d->audio.fmt.ileaved = 0;
			d->datatype = "flac";
			f->sample_rate = info->sample_rate;
			break;
		}

		case FLACOGGREAD_TAG:
			flacogg_meta(f, d);
			break;

		case FLACOGGREAD_HEADER_FIN: {
			if (d->input_info)
				return FMED_RDONE;

			const struct flac_info *info = flacoggread_info(&f->fo);
			if (info->minblock != info->maxblock) {
				errlog(d->trk, "unsupported case: minblock != maxblock");
				return FMED_RERR;
			}

			d->flac_minblock = info->minblock;
			d->flac_maxblock = info->maxblock;
			f->fr_samples = info->minblock;

			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "flac.decode"))
				return FMED_RERR;
			break;
		}

		case FLACOGGREAD_DATA:
			goto data;

		case FLACOGGREAD_MORE:
			if (d->flags & FMED_FLAST)
				return FMED_RDONE;
			return FMED_RMORE;

		case FLACOGGREAD_ERROR:
			errlog(d->trk, "flacoggread_read(): %s", flacoggread_error(&f->fo));
			return FMED_RERR;

		default:
			errlog(d->trk, "flacoggread_read(): %r", r);
			return FMED_RERR;
		}
	}

data:
	if (f->seek_ms != (uint64)-1) {
		uint64 seek = ffpcm_samples(f->seek_ms, f->sample_rate);
		dbglog(d->trk, "seek: %U @%U", seek, f->apos);
		if (seek >= f->apos + f->fr_samples) {
			f->apos += f->fr_samples;
			return FMED_RMORE;
		}
		f->seek_ms = (uint64)-1;
	}

	dbglog(d->trk, "frame size:%L  @%U", out.len, f->apos);
	d->audio.pos = f->apos;
	d->flac_samples = f->fr_samples;
	f->apos += f->fr_samples;
	d->out = out.ptr,  d->outlen = out.len;
	return FMED_RDATA;
}
