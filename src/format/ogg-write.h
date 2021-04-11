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
	ffvec pkt;
	ffstr in;
	uint state;
	uint64 total;
	uint add_opus_tags;
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

	if (fmed_getval("opus_no_tags") == 1)
		o->add_opus_tags = 1;

	return o;
}

void ogg_out_close(void *ctx)
{
	struct ogg_out *o = ctx;
	oggwrite_close(&o->og);
	ffvec_free(&o->pkt);
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

/*
Per-packet writing ("end-pos" value is optional):
. while "start-pos" == 0: write packet and flush
. write the cached packet with "end-pos" = "input start-pos"
. store the input packet in temp. buffer
. ask for more data; if no more data:
  . write the input packet with "end-pos" = "input start-pos" +1; flush and exit

OGG->OGG copying doesn't require temp. buffer.
*/
int ogg_out_encode(void *ctx, fmed_filt *d)
{
	enum { I_CONF, I_PKT, I_PAGE_EXACT };
	struct ogg_out *o = ctx;
	int r;
	ffstr in = d->data_in, out;
	uint flags = 0;
	int64 gp;
	uint64 endpos = d->audio.pos; // end-pos = start-pos of the next packet

	for (;;) {
		switch (o->state) {

		case I_CONF: {
			o->state = I_PKT;
			uint max_page_samples = 44100;
			if (d->audio.convfmt.sample_rate != 0)
				max_page_samples = ffpcm_samples(wconf.max_page_duration, d->audio.convfmt.sample_rate);
			if (ffsz_eq(d->datatype, "OGG")) {
				max_page_samples = 0; // ogg->ogg copy must replicate the pages exactly
				o->state = I_PAGE_EXACT;
			}

			if (0 != oggwrite_create(&o->og, ffrnd_get(), max_page_samples)) {
				errlog1(d->trk, "oggwrite_create() failed");
				return FMED_RERR;
			}

			if (ffsz_eq(d->datatype, "pcm")) {
				const char *ofn = d->track->getvalstr(d->trk, "output");
				const char *enc = ogg_enc_mod(ofn);
				if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)enc))
					return FMED_RERR;
			}

			continue;
		}

		case I_PKT:
			if (endpos == 0) {
				flags = OGGWRITE_FFLUSH;

			} else if (o->pkt.len == 0) {
				if (!(d->flags & FMED_FLAST)) {
					ffvec_add2(&o->pkt, &d->data_in, 1); // cache the first data packet
					return FMED_RMORE;
				}

			} else {
				ffstr_set2(&in, &o->pkt); // gonna write the cached packet
			}

			if (d->flags & FMED_FLAST) {
				flags = OGGWRITE_FLAST;
				if ((gp = fmed_getval("ogg_granpos")) > 0) {
					endpos = gp; // encoder set end-pos value
				} else {
					// don't know the packet's audio length -> can't set the real end-pos value
					endpos = endpos+1;
				}
			}
			goto write;

		case I_PAGE_EXACT:
			if (d->ogg_flush) {
				d->ogg_flush = 0;
				flags = OGGWRITE_FFLUSH;
			}

			if (d->flags & FMED_FLAST)
				flags |= OGGWRITE_FLAST;

			endpos = fmed_getval("ogg_granpos");
			goto write;
		}
	}

write:
	if (o->add_opus_tags && o->og.stat.npkts == 1) {
		static const char opus_tags[] = "OpusTags\x08\x00\x00\x00" "datacopy\x00\x00\x00\x00";
		ffstr_set(&in, opus_tags, sizeof(opus_tags)-1);
		r = oggwrite_process(&o->og, &in, &out, 0, OGGWRITE_FFLUSH);
		if (r != OGGWRITE_DATA) {
			errlog1(d->trk, "oggwrite_process(opus_tags)");
			return FMED_RERR;
		}
		goto data;
	}

	r = oggwrite_process(&o->og, &in, &out, endpos, flags);

	switch (r) {

	case OGGWRITE_DONE:
		infolog1(d->trk, "OGG: packets:%U, pages:%U, overhead: %.2F%%"
			, (int64)o->og.stat.npkts, (int64)o->og.stat.npages
			, (double)o->og.stat.total_ogg * 100 / (o->og.stat.total_payload + o->og.stat.total_ogg));
		return FMED_RLASTOUT;

	case OGGWRITE_DATA:
		break;

	case OGGWRITE_MORE:
		o->pkt.len = 0;
		ffvec_add2(&o->pkt, &d->data_in, 1); // cache the current data packet
		return FMED_RMORE;

	default:
		errlog1(d->trk, "oggwrite_process() failed");
		return FMED_RERR;
	}

	if (in.len == 0) {
		o->pkt.len = 0;
		ffvec_add2(&o->pkt, &d->data_in, 1); // cache the current data packet
		d->data_in.len = 0;
	}

data:
	o->total += out.len;
	dbglog1(d->trk, "output: %L bytes (%U), page:%U, end-pos:%U"
		, out.len, o->total, (int64)o->og.stat.npages-1, endpos);
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter ogg_output = {
	ogg_out_open, ogg_out_encode, ogg_out_close
};
