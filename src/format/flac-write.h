/** fmedia: .flac writer
2018, Simon Zolin */

#include <fmedia.h>

#include <avpack/flac-write.h>
#include <FF/audio/flac.h>
#include <FF/mtags/mmtag.h>
#include <FF/pic/png.h>
#include <FF/pic/jpeg.h>

typedef struct flac_out {
	flacwrite fl;
	ffstr in;
	uint state;
} flac_out;

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
	ffstr name, *val;

	ffstr vendor = FFSTR_INITZ(flac_vendor());
	if (0 != flacwrite_addtag(&f->fl, MMTAG_VENDOR, vendor)) {
		syserrlog(d->trk, "can't add tag: %S", &name);
		return -1;
	}

	void *qent;
	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (uint i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| ffstr_eqcz(&name, "vendor"))
			continue;

		if (ffstr_eqcz(&name, "picture")) {
			struct flac_picinfo info = {};
			pic_meta(&info, val, d->trk);
			flacwrite_pic(&f->fl, &info, val);
			continue;
		}

		if (0 != flacwrite_addtag_name(&f->fl, name, *val)) {
			syserrlog(d->trk, "can't add tag: %S", &name);
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
	return f;
}

static void flac_out_free(void *ctx)
{
	flac_out *f = ctx;
	flacwrite_close(&f->fl);
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
			errlog(d->trk, "unsupported input data format: %s", d->datatype);
			return FMED_RERR;
		}

		if (d->datalen != sizeof(struct flac_info)) {
			errlog(d->trk, "invalid first input data block");
			return FMED_RERR;
		}

		ffuint64 total_samples = 0;
		if ((int64)d->audio.total != FMED_NULL && d->out_seekable)
			total_samples = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;

		struct flac_info *info = (void*)d->data;

		flacwrite_create(&f->fl, info, total_samples);
		f->fl.seektable_interval = flac_out_conf.sktab_int * d->audio.convfmt.sample_rate;
		f->fl.min_meta = flac_out_conf.min_meta_size;

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
		ffstr_set(&f->in, (const void**)d->datani, d->datalen);
		if (d->flags & FMED_FLAST) {
			if (d->datalen != sizeof(struct flac_info)) {
				errlog(d->trk, "invalid last input data block");
				return FMED_RERR;
			}
			flacwrite_finish(&f->fl, (void*)d->data);
		}
	}

	ffstr out = {};

	for (;;) {
		r = flacwrite_process(&f->fl, &f->in, fmed_getval("flac_in_frsamples"), &out);

		switch (r) {
		case FLACWRITE_MORE:
			return FMED_RMORE;

		case FLACWRITE_DATA:
			if (f->state == I_DATA0) {
				f->state = I_DATA;
			}
			goto data;

		case FLACWRITE_DONE:
			goto data;

		case FLACWRITE_SEEK:
			d->output.seek = flacwrite_offset(&f->fl);
			continue;

		case FLACWRITE_ERROR:
		default:
			errlog(d->trk, "flacwrite_process(): %s", flacwrite_error(&f->fl));
			return FMED_RERR;
		}
	}

data:
	dbglog(d->trk, "output: %L bytes", out.len);
	d->out = out.ptr;
	d->outlen = out.len;
	if (r == FLACWRITE_DONE)
		return FMED_RDONE;
	return FMED_ROK;
}

const fmed_filter fmed_flac_output = {
	flac_out_create, flac_out_encode, flac_out_free
};