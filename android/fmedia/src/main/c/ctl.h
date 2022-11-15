/** fmedia/Android: control filter
2022, Simon Zolin */

static void* ctl_open(fmed_filt *d)
{
	return FMED_FILT_DUMMY;
}

static void ctl_close(void *ctx)
{
}

static int ctl_process(void *ctx, fmed_track_info *d)
{
	if (d->flags & FMED_FFIRST)
		return FMED_RFIN;
	return FMED_RMORE;
}

const fmed_filter ctl_filter = {
	ctl_open, ctl_process, ctl_close
};
