/** fmedia: .ogg write
2015,2021, Simon Zolin */

#include <avpack/ogg-write.h>
#include <FFOS/random.h>

struct ogg_out_conf_t {
	ushort max_page_duration;
} wconf;

const fmed_conf_arg ogg_out_conf_args[] = {
	{ "max_page_duration",  FMC_INT16,  FMC_O(struct ogg_out_conf_t, max_page_duration) },
	{}
};

int ogg_out_conf(fmed_conf_ctx *ctx)
{
	wconf.max_page_duration = 1000;
	fmed_conf_addctx(ctx, &wconf, ogg_out_conf_args);
	return 0;
}


struct ogg_out {
	oggwrite og;
	ffvec pktbuf;
	ffstr pkt;
	uint state;
	uint64 total;
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
	ffvec_free(&o->pktbuf);
	ffmem_free(o);
}

const char* ogg_enc_mod(const char *fn)
{
	ffstr name, ext;
	ffpath_splitpath(fn, ffsz_len(fn), NULL, &name);
	ffstr_rsplitby(&name, '.', NULL, &ext);
	if (ffstr_eqcz(&ext, "opus"))
		return "opus.encode";
	return "vorbis.encode";
}

int pkt_write(struct ogg_out *o, fmed_filt *d, ffstr *in, ffstr *out, uint64 endpos, uint flags)
{
	dbglog1(d->trk, "oggwrite_process(): size:%L  endpos:%U flags:%u", in->len, endpos, flags);
	int r = oggwrite_process(&o->og, in, out, endpos, flags);

	switch (r) {

	case OGGWRITE_DONE:
		infolog1(d->trk, "OGG: packets:%U, pages:%U, overhead: %.2F%%"
			, (int64)o->og.stat.npkts, (int64)o->og.stat.npages
			, (double)o->og.stat.total_ogg * 100 / (o->og.stat.total_payload + o->og.stat.total_ogg));
		return FMED_RLASTOUT;

	case OGGWRITE_DATA:
		break;

	case OGGWRITE_MORE:
		return FMED_RMORE;

	default:
		errlog1(d->trk, "oggwrite_process() failed");
		return FMED_RERR;
	}

	o->total += out->len;
	dbglog1(d->trk, "output: %L bytes (%U), page:%U, end-pos:%D"
		, out->len, o->total, (int64)o->og.stat.npages-1, o->og.page_startpos);
	return FMED_RDATA;
}

/*
Per-packet writing ("end-pos" value is optional):
. while "start-pos" == 0: write packet and flush
. if add_opus_tags==1: write packet #2 with Opus tags
. write the cached packet with "end-pos" = "current start-pos"
. store the input packet in temp. buffer
. ask for more data; if no more data:
  . write the input packet with "end-pos" = "input start-pos" +1; flush and exit

OGG->OGG copying doesn't require temp. buffer.
*/
int ogg_out_encode(void *ctx, fmed_filt *d)
{
	enum { I_CONF, I_PKT, I_OPUS_TAGS, I_PAGE_EXACT };
	struct ogg_out *o = ctx;
	int r;
	ffstr in;
	uint flags = 0;
	uint64 endpos;

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
				const char *enc = ogg_enc_mod(d->out_filename);
				if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)enc))
					return FMED_RERR;
			}

			if (o->state == I_PKT && !(d->flags & FMED_FLAST)) {
				ffvec_add2(&o->pktbuf, &d->data_in, 1); // store the first packet
				return FMED_RMORE;
			}

			continue;
		}

		case I_PKT:
			endpos = d->audio.pos; // end-pos (for previous packet) = start-pos of this packet
			if (o->og.stat.npkts == 0) {
				endpos = 0;
				if (d->ogg_gen_opus_tag)
					o->state = I_OPUS_TAGS;
			}
			if (endpos == 0)
				flags = OGGWRITE_FFLUSH;
			if ((d->flags & FMED_FLAST) && d->data_in.len == 0)
				flags = OGGWRITE_FLAST;

			r = pkt_write(o, d, &o->pkt, &d->data_out, endpos, flags);
			if (r == FMED_RMORE) {
				if (d->flags & FMED_FLAST) {
					if ((int64)d->audio.total != FMED_NULL && endpos < d->audio.total)
						endpos = d->audio.total;
					else
						endpos++; // we don't know the packet's audio length -> can't set the real end-pos value
					return pkt_write(o, d, &d->data_in, &d->data_out, endpos, OGGWRITE_FLAST);
				}
				o->pktbuf.len = 0;
				ffvec_add2(&o->pktbuf, &d->data_in, 1); // store the current data packet
				d->data_in.len = 0;
				ffstr_set2(&o->pkt, &o->pktbuf);
			}
			return r;

		case I_OPUS_TAGS: {
			o->state = I_PKT;
			static const char opus_tags[] = "OpusTags\x08\x00\x00\x00" "datacopy\x00\x00\x00\x00";
			ffstr_set(&in, opus_tags, sizeof(opus_tags)-1);
			return pkt_write(o, d, &in, &d->data_out, 0, OGGWRITE_FFLUSH);
		}

		case I_PAGE_EXACT:
			if (d->ogg_flush) {
				d->ogg_flush = 0;
				flags = OGGWRITE_FFLUSH;
			}

			if (d->flags & FMED_FLAST)
				flags = OGGWRITE_FLAST;

			endpos = d->ogg_granule_pos;
			r = pkt_write(o, d, &d->data_in, &d->data_out, endpos, flags);
			return r;
		}
	}
}

const fmed_filter ogg_output = {
	ogg_out_open, ogg_out_encode, ogg_out_close
};
