/** fmedia/Android: control filter
2022, Simon Zolin */

extern const fmed_core *core;

static void* ctl_open(fmed_filt *ti)
{
	return FMED_FILT_DUMMY;
}

static void ctl_close(void *ctx)
{
}

static int ctl_process(void *ctx, fmed_track_info *ti)
{
	if (ti->stream_copy && !ffsz_eq(ti->datatype, "mpeg")) {
		errlog1(ti->trk, "Currently only MPEG1-L3 is supported");
		ti->error = FMED_E_UNKIFMT;
		return FMED_RERR;
	}

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
