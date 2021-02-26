/** fmedia: .ogg write
2015,2021, Simon Zolin */

#include <avpack/ogg-write.h>
#include <FFOS/random.h>

struct ogg_out_conf_t {
	ushort max_page_duration;
} wconf;

const ffpars_arg ogg_out_conf_args[] = {
	{ "max_page_duration",  FFPARS_TINT | FFPARS_F16BIT,  FFPARS_DSTOFF(struct ogg_out_conf_t, max_page_duration) },
};

int ogg_out_conf(ffpars_ctx *ctx)
{
	wconf.max_page_duration = 1000;
	ffpars_setargs(ctx, &wconf, ogg_out_conf_args, FF_COUNT(ogg_out_conf_args));
	return 0;
}


struct ogg_out {
	oggwrite og;
	uint state;
};

void* ogg_out_open(fmed_filt *d)
{
	struct ogg_out *o = ffmem_new(struct ogg_out);
	if (o == NULL)
		return NULL;

	static ffbyte seed;
	if (!seed) {
		seed = 1;
		fftime t;
		fftime_now(&t);
		ffrnd_seed(fftime_sec(&t));
	}

	return o;
}

void ogg_out_close(void *ctx)
{
	struct ogg_out *o = ctx;
	oggwrite_close(&o->og);
	ffmem_free(o);
}

const char* ogg_enc_mod(const char *fn)
{
	ffstr name, ext;
	ffpath_splitpath(fn, ffsz_len(fn), NULL, &name);
	ffs_rsplit2by(name.ptr, name.len, '.', NULL, &ext);
	if (ffstr_eqcz(&ext, "opus"))
		return "opus.encode";
	return "vorbis.encode";
}

int ogg_out_encode(void *ctx, fmed_filt *d)
{
	enum { I_CONF, I_CREAT, I_ENCODE };
	struct ogg_out *o = ctx;
	int r;

	switch (o->state) {
	case I_CONF: {

		if (ffsz_eq(d->datatype, "OGG")) {
			if (0 != (r = oggwrite_create(&o->og, ffrnd_get(), 0))) {
				errlog1(d->trk, "oggwrite_create() failed");
				return FMED_RERR;
			}

			o->state = I_ENCODE;
			break;

		} else if (!ffsz_eq(d->datatype, "pcm")) {
			errlog1(d->trk, "unsupported input data format: %s", d->datatype);
			return FMED_RERR;
		}

		const char *enc = ogg_enc_mod(d->track->getvalstr(d->trk, "output"));
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)enc)) {
			return FMED_RERR;
		}
		o->state = I_CREAT;
		return FMED_RMORE;
	}

	case I_CREAT: {
		ffuint max_pagedelta = ffpcm_samples(wconf.max_page_duration, d->audio.convfmt.sample_rate);
		if (0 != (r = oggwrite_create(&o->og, ffrnd_get(), max_pagedelta))) {
			errlog1(d->trk, "oggwrite_create() failed");
			return FMED_RERR;
		}
		o->state = I_ENCODE;
	}
		// fallthrough

	case I_ENCODE:
		break;
	}

	int flags = 0;
	if (d->flags & FMED_FFWD) {
		flags |= (d->ogg_flush) ? OGGWRITE_FFLUSH : 0;
		d->ogg_flush = 0;
		flags |= !!(d->flags & FMED_FLAST) ? OGGWRITE_FLAST : 0;
	}

	r = oggwrite_process(&o->og, &d->data_in, &d->data_out, fmed_getval("ogg_granpos"), flags);
	switch (r) {

	case OGGWRITE_DONE:
		core->log(FMED_LOG_INFO, d->trk, NULL, "OGG: packets:%U, pages:%U, overhead: %.2F%%"
			, o->og.stat.npkts, o->og.stat.npages
			, (double)o->og.stat.total_ogg * 100 / (o->og.stat.total_payload + o->og.stat.total_ogg));
		d->data_out.len = 0;
		return FMED_RLASTOUT;

	case OGGWRITE_DATA:
		goto data;

	case OGGWRITE_MORE:
		return FMED_RMORE;

	default:
		errlog1(d->trk, "oggwrite_process() failed");
		return FMED_RERR;
	}

data:
	dbglog1(d->trk, "output: %L bytes, page: %u"
		, d->data_out.len, o->og.page.number-1);

	return FMED_RDATA;
}

const fmed_filter ogg_output = {
	ogg_out_open, ogg_out_encode, ogg_out_close
};
