/** fmedia: .wv reader
2021, Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>
#include <avpack/wv-read.h>

extern const fmed_core *core;

typedef struct wvpk {
	wvread wv;
	ffstr in;
	uint state;
	uint sample_rate;
	uint hdr_done;
} wvpk;

static void* wv_in_create(fmed_filt *d)
{
	wvpk *w = ffmem_new(wvpk);
	ffuint64 fs = 0;
	if ((int64)d->input.size != FMED_NULL)
		fs = d->input.size;
	wvread_open(&w->wv, fs);
	return w;
}

static void wv_in_free(void *ctx)
{
	wvpk *w = ctx;
	wvread_close(&w->wv);
	ffmem_free(w);
}

static void wv_in_meta(wvpk *w, fmed_filt *d)
{
	ffstr name, val;
	int tag = wvread_tag(&w->wv, &name, &val);
	if (tag != 0)
		ffstr_setz(&name, ffmmtag_str[tag]);
	dbglog(core, d->trk, "wvpk", "tag: %S: %S", &name, &val);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static int wv_in_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_HDR_PARSED, I_DATA };
	wvpk *w = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_setstr(&w->in, &d->data_in);
		d->data_in.len = 0;
	}

	switch (w->state) {
	case I_HDR:
		break;

	case I_HDR_PARSED:
		w->sample_rate = d->audio.fmt.sample_rate;
		w->state = I_DATA;
		// fallthrough

	case I_DATA:
		if (d->seek_req && (int64)d->audio.seek != FMED_NULL) {
			d->seek_req = 0;
			wvread_seek(&w->wv, ffpcm_samples(d->audio.seek, w->sample_rate));
			fmed_dbglog(core, d->trk, "wvpk", "seek: %Ums", d->audio.seek);
		}
		break;
	}

	for (;;) {
		r = wvread_process(&w->wv, &w->in, &d->data_out);
		switch (r) {
		case WVREAD_ID31:
		case WVREAD_APETAG:
			wv_in_meta(w, d);
			break;

		case WVREAD_DATA:
			if (!w->hdr_done) {
				w->hdr_done = 1;
				const struct wvread_info *info = wvread_info(&w->wv);
				d->audio.total = info->total_samples;
				if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "wavpack.decode"))
					return FMED_RERR;
				w->state = I_HDR_PARSED;
			}
			goto data;

		case WVREAD_SEEK:
			d->input.seek = wvread_offset(&w->wv);
			return FMED_RMORE;

		case WVREAD_MORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RLASTOUT;
			}
			return FMED_RMORE;

		case WVREAD_WARN:
			warnlog(core, d->trk, "wvpk", "wvread_read(): at offset %xU: %s"
				, wvread_offset(&w->wv), wvread_error(&w->wv));
			break;

		case WVREAD_ERROR:
			errlog(core, d->trk, "wvpk", "wvread_read(): %s", wvread_error(&w->wv));
			return FMED_RERR;
		}
	}

data:
	dbglog(core, d->trk, "wvpk", "frame: %L bytes (@%U)"
		, d->data_out.len, wvread_cursample(&w->wv));
	d->audio.pos = wvread_cursample(&w->wv);
	return FMED_RDATA;
}

const fmed_filter wv_input = {
	wv_in_create, wv_in_process, wv_in_free
};
