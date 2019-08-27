/** FLAC-OGG input.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/flac.h>
#include <FF/audio/flac.h>


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
	ffflac_ogg fo;
	uint64 apos;
};

static void* flacogg_in_create(fmed_filt *d)
{
	struct flacogg_in *f = ffmem_new(struct flacogg_in);
	if (f == NULL)
		return NULL;

	if (0 != ffflac_ogg_open(&f->fo)) {
		ffmem_free(f);
		return NULL;
	}
	return f;
}

static void flacogg_in_free(void *ctx)
{
	struct flacogg_in *f = ctx;
	ffflac_ogg_close(&f->fo);
	ffmem_free(f);
}

static int flacogg_in_read(void *ctx, fmed_filt *d)
{
	struct flacogg_in *f = ctx;
	int r;
	ffstr out;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD)
		ffflac_ogg_input(&f->fo, d->data, d->datalen);

	for (;;) {
		r = ffflac_ogg_read(&f->fo);

		switch (r) {
		case FFFLAC_RHDR:
			d->audio.decoder = "FLAC";
			ffpcm_fmtcopy(&d->audio.fmt, &f->fo.fmt);
			d->audio.fmt.ileaved = 0;
			d->datatype = "flac";
			break;

		case FFFLAC_RTAG:
			dbglog(d->trk, "meta block type %xu", f->fo.meta_type);
			break;

		case FFFLAC_RHDRFIN:
			if (d->input_info)
				return FMED_RDONE;

			if (f->fo.info.minblock != 4096 || f->fo.info.maxblock != 4096)
				return FMED_RERR;

			fmed_setval("flac.in.minblock", 4096);
			fmed_setval("flac.in.maxblock", 4096);

			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "flac.decode"))
				return FMED_RERR;
			break;

		case FFFLAC_RDATA:
			goto data;

		case FFFLAC_RMORE:
			return FMED_RMORE;

		case FFFLAC_RERR:
			errlog(d->trk, "ffflac_ogg_read(): %s", ffflac_ogg_errstr(&f->fo));
			return FMED_RERR;

		default:
			return FMED_RERR;
		}
	}

data:
	out = ffflac_ogg_output(&f->fo);
	dbglog(d->trk, "read frame.  size:%L", out.len);
	d->audio.pos = f->apos;
	fmed_setval("flac.in.frsamples", 4096);
	fmed_setval("flac.in.frpos", f->apos);
	f->apos += 4096;
	d->out = out.ptr,  d->outlen = out.len;
	return FMED_RDATA;
}
