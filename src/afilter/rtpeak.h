/** fmedia: real-time peaks filter
2015,2022 Simon Zolin */

#include <fmedia.h>
#include <afilter/pcm.h>

struct rtpeak {
	ffpcmex fmt;
};

static void* rtpeak_open(fmed_filt *d)
{
	struct rtpeak *p = ffmem_new(struct rtpeak);
	if (p == NULL)
		return NULL;
	p->fmt = d->audio.fmt;
	double t;
	if (0 != ffpcm_peak(&p->fmt, NULL, 0, &t)) {
		errlog(core, d->trk, "rtpeak", "ffpcm_peak(): unsupported format");
		ffmem_free(p);
		return FMED_FILT_SKIP;
	}
	return p;
}

static void rtpeak_close(void *ctx)
{
	struct rtpeak *p = ctx;
	ffmem_free(p);
}

static int rtpeak_process(void *ctx, fmed_filt *d)
{
	struct rtpeak *p = ctx;

	double maxpeak;
	ffpcm_peak(&p->fmt, d->data, d->datalen / ffpcm_size1(&p->fmt), &maxpeak);
	double db = ffpcm_gain2db(maxpeak);
	d->audio.maxpeak = db;
	dbglog(core, d->trk, "rtpeak", "maxpeak:%.2F", db);

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}

static const fmed_filter fmed_sndmod_rtpeak = { rtpeak_open, rtpeak_process, rtpeak_close };
