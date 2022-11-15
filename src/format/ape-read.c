/** fmedia: .ape reader
2021, Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>
#include <avpack/ape-read.h>

extern const fmed_core *core;

typedef struct ape {
	aperead ape;
	ffstr in;
	uint state;
	uint sample_rate;
} ape;

static void* ape_in_create(fmed_filt *d)
{
	ape *a = ffmem_new(ape);
	ffuint64 fs = 0;
	if ((int64)d->input.size != FMED_NULL)
		fs = d->input.size;
	aperead_open(&a->ape, fs);
	a->ape.id3v1.codepage = core->props->codepage;
	return a;
}

static void ape_in_free(void *ctx)
{
	ape *a = ctx;
	aperead_close(&a->ape);
	ffmem_free(a);
}

static void ape_in_meta(ape *a, fmed_filt *d)
{
	ffstr name, val;
	int tag = aperead_tag(&a->ape, &name, &val);
	if (tag != 0)
		ffstr_setz(&name, ffmmtag_str[tag]);
	dbglog(core, d->trk, "ape", "tag: %S: %S", &name, &val);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static int ape_in_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_HDR_PARSED, I_DATA };
	ape *a = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_setstr(&a->in, &d->data_in);
		d->data_in.len = 0;
	}

	switch (a->state) {
	case I_HDR:
		break;

	case I_HDR_PARSED:
		a->sample_rate = d->audio.fmt.sample_rate;
		a->state = I_DATA;
		// fallthrough

	case I_DATA:
		if (d->seek_req && (int64)d->audio.seek != FMED_NULL) {
			d->seek_req = 0;
			aperead_seek(&a->ape, ffpcm_samples(d->audio.seek, a->sample_rate));
			fmed_dbglog(core, d->trk, "ape", "seek: %Ums", d->audio.seek);
		}
		break;
	}

	for (;;) {
		r = aperead_process(&a->ape, &a->in, &d->data_out);
		switch (r) {
		case APEREAD_ID31:
		case APEREAD_APETAG:
			ape_in_meta(a, d);
			break;

		case APEREAD_HEADER:
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "ape.decode"))
				return FMED_RERR;
			a->state = I_HDR_PARSED;
			return FMED_RDATA;

		case APEREAD_DONE:
		case APEREAD_DATA:
			goto data;

		case APEREAD_SEEK:
			d->input.seek = aperead_offset(&a->ape);
			return FMED_RMORE;

		case APEREAD_MORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RLASTOUT;
			}
			return FMED_RMORE;

		case APEREAD_WARN:
			warnlog(core, d->trk, "ape", "aperead_read(): at offset %xU: %s"
				, aperead_offset(&a->ape), aperead_error(&a->ape));
			break;

		case APEREAD_ERROR:
			errlog(core, d->trk, "ape", "aperead_read(): %s", aperead_error(&a->ape));
			return FMED_RERR;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "ape", "frame: %L bytes (@%U)"
		, d->data_out.len, aperead_cursample(&a->ape));
	d->audio.pos = aperead_cursample(&a->ape);
	fmed_setval("ape_block_samples", aperead_block_samples(&a->ape));
	fmed_setval("ape_align4", aperead_align4(&a->ape));
	return (r == APEREAD_DATA) ? FMED_RDATA : FMED_RLASTOUT;
}

const fmed_filter ape_input = {
	ape_in_create, ape_in_process, ape_in_free
};
