/** fmedia/Android: control filter
2022, Simon Zolin */

#include <util/path.h>

extern const fmed_core *core;

static void* ctl_open(fmed_track_info *ti)
{
	if (ti->stream_copy
		&& !(ffsz_eq(ti->datatype, "mpeg")
			|| ffsz_eq(ti->datatype, "mp4"))) {
		errlog1(ti->trk, "Currently only .mp3 & .mp4 are supported");
		ti->error = FMED_E_UNKIFMT;
		return NULL;
	}

	// prevent .m4a->.mp3 copy
	if (ti->stream_copy && ffsz_eq(ti->datatype, "mp4")) {
		ffstr ext;
		ffpath_split3(ti->out_filename, ffsz_len(ti->out_filename), NULL, NULL, &ext);
		if (ffstr_eqz(&ext, "mp3")) {
			errlog1(ti->trk, ".mp3 & .mp4 are incompatible");
			ti->error = FMED_E_INCOMPATFMT;
			return NULL;
		}
	}

	return FMED_FILT_DUMMY;
}

static void ctl_close(void *ctx)
{
}

static int ctl_process(void *ctx, fmed_track_info *ti)
{
	ti->data_out = ti->data_in;
	if (ti->flags & FMED_FFIRST) {
		if (ti->out_preserve_date)
			ti->out_mtime = ti->in_mtime;
		return FMED_RDONE;
	}
	return FMED_ROK;
}

const fmed_filter ctl_filter = {
	ctl_open, ctl_process, ctl_close
};
