/** FLAC output.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/flac.h>
#include <FF/audio/flac.h>
#include <FF/mtags/mmtag.h>
#include <FF/pic/png.h>
#include <FF/pic/jpeg.h>


#undef warnlog
#define warnlog(trk, ...)  fmed_warnlog(core, trk, "flac.out", __VA_ARGS__)


extern const fmed_core *core;
extern const fmed_queue *qu;


//OUT
static void* flac_out_create(fmed_filt *d);
static void flac_out_free(void *ctx);
static int flac_out_encode(void *ctx, fmed_filt *d);
const fmed_filter fmed_flac_output = {
	&flac_out_create, &flac_out_encode, &flac_out_free
};

typedef struct flac_out {
	ffflac_cook fl;
	uint state;
} flac_out;

static int flac_out_addmeta(flac_out *f, fmed_filt *d);

static struct flac_out_conf_t {
	uint sktab_int;
	uint min_meta_size;
} flac_out_conf;

static const ffpars_arg flac_out_conf_args[] = {
	{ "min_meta_size",  FFPARS_TINT,  FFPARS_DSTOFF(struct flac_out_conf_t, min_meta_size) },
	{ "seektable_interval",	FFPARS_TINT,  FFPARS_DSTOFF(struct flac_out_conf_t, sktab_int) },
};


int flac_out_config(ffpars_ctx *conf)
{
	flac_out_conf.sktab_int = 1;
	flac_out_conf.min_meta_size = 1000;
	ffpars_setargs(conf, &flac_out_conf, flac_out_conf_args, FFCNT(flac_out_conf_args));
	return 0;
}

static int pic_meta_png(struct flac_picinfo *info, const ffstr *data)
{
	struct ffpngr png = {};
	int rc = -1, r;
	if (0 != ffpngr_open(&png))
		goto err;
	png.input = *data;
	r = ffpngr_read(&png);
	if (r != FFPNG_HDR)
		goto err;

	info->mime = FFPNG_MIME;
	info->width = png.info.width;
	info->height = png.info.height;
	info->bpp = png.info.bpp;
	rc = 0;

err:
	ffpngr_close(&png);
	return rc;
}

static int pic_meta_jpeg(struct flac_picinfo *info, const ffstr *data)
{
	struct ffjpegr jpeg = {};
	int rc = -1, r;
	if (0 != ffjpegr_open(&jpeg))
		goto err;
	jpeg.input = *data;
	r = ffjpegr_read(&jpeg);
	if (r != FFJPEG_HDR)
		goto err;

	info->mime = FFJPEG_MIME;
	info->width = jpeg.info.width;
	info->height = jpeg.info.height;
	info->bpp = jpeg.info.bpp;
	rc = 0;

err:
	ffjpegr_close(&jpeg);
	return rc;
}

static void pic_meta(struct flac_picinfo *info, const ffstr *data, void *trk)
{
	if (0 == pic_meta_png(info, data))
		return;
	if (0 == pic_meta_jpeg(info, data))
		return;
	warnlog(trk, "picture write: can't detect MIME; writing without MIME and image dimensions");
}

static int flac_out_addmeta(flac_out *f, fmed_filt *d)
{
	uint i;
	ffstr name, *val;
	void *qent;

	const char *vendor = flac_vendor();
	if (0 != ffflac_addtag(&f->fl, NULL, vendor, ffsz_len(vendor))) {
		syserrlog(core, d->trk, "flac", "can't add tag: %S", &name);
		return -1;
	}

	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| ffstr_eqcz(&name, "vendor"))
			continue;

		if (ffstr_eqcz(&name, "picture")) {
			struct flac_picinfo info = {};
			pic_meta(&info, val, d->trk);
			ffflac_setpic(&f->fl, &info, val);
			continue;
		}

		if (0 != ffflac_addtag(&f->fl, name.ptr, val->ptr, val->len)) {
			syserrlog(core, d->trk, "flac", "can't add tag: %S", &name);
			return -1;
		}
	}
	return 0;
}

static void* flac_out_create(fmed_filt *d)
{
	flac_out *f = ffmem_tcalloc1(flac_out);
	if (f == NULL)
		return NULL;

	ffflac_winit(&f->fl);
	if (!d->out_seekable) {
		f->fl.seekable = 0;
		f->fl.seektable_int = 0;
	}
	return f;
}

static void flac_out_free(void *ctx)
{
	flac_out *f = ctx;
	ffflac_wclose(&f->fl);
	ffmem_free(f);
}

static int flac_out_encode(void *ctx, fmed_filt *d)
{
	enum { I_FIRST, I_INIT, I_DATA0, I_DATA };
	flac_out *f = ctx;
	int r;

	switch (f->state) {
	case I_FIRST:
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, "flac.encode"))
			return FMED_RERR;
		f->state = I_INIT;
		return FMED_RMORE;

	case I_INIT:
		if (!ffsz_eq(d->datatype, "flac")) {
			errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
			return FMED_RERR;
		}

		if ((int64)d->audio.total != FMED_NULL)
			f->fl.total_samples = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;

		f->fl.seektable_int = flac_out_conf.sktab_int * d->audio.convfmt.sample_rate;
		f->fl.min_meta = flac_out_conf.min_meta_size;

		if (d->datalen != sizeof(ffflac_info)) {
			errlog(core, d->trk, NULL, "invalid first input data block");
			return FMED_RERR;
		}

		if (0 != ffflac_wnew(&f->fl, (void*)d->data)) {
			errlog(core, d->trk, "flac", "ffflac_wnew(): %s", ffflac_out_errstr(&f->fl));
			return FMED_RERR;
		}
		d->datalen = 0;
		if (0 != flac_out_addmeta(f, d))
			return FMED_RERR;

		f->state = I_DATA0;
		break;

	case I_DATA0:
	case I_DATA:
		break;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_set(&f->fl.in, (const void**)d->datani, d->datalen);
		if (d->flags & FMED_FLAST) {
			if (d->datalen != sizeof(ffflac_info)) {
				errlog(core, d->trk, NULL, "invalid last input data block");
				return FMED_RERR;
			}
			ffflac_wfin(&f->fl, (void*)d->data);
		}
	}

	for (;;) {
	r = ffflac_write(&f->fl, fmed_getval("flac_in_frsamples"));

	switch (r) {
	case FFFLAC_RMORE:
		return FMED_RMORE;

	case FFFLAC_RDATA:
		if (f->state == I_DATA0) {
			d->output.size = ffflac_wsize(&f->fl);
			f->state = I_DATA;
		}
		goto data;

	case FFFLAC_RDONE:
		goto data;

	case FFFLAC_RSEEK:
		d->output.seek = f->fl.seekoff;
		continue;

	case FFFLAC_RERR:
	default:
		errlog(core, d->trk, "flac", "ffflac_write(): %s", ffflac_out_errstr(&f->fl));
		return FMED_RERR;
	}
	}

data:
	dbglog(core, d->trk, "flac", "output: %L bytes", f->fl.out.len);
	d->out = f->fl.out.ptr;
	d->outlen = f->fl.out.len;
	if (r == FFFLAC_RDONE)
		return FMED_RDONE;
	return FMED_ROK;
}
