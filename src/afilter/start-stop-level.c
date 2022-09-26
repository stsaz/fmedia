/** Start-level trigger;  stop-level trigger.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>

extern const fmed_core *core;

#define infolog(trk, ...)  fmed_infolog(core, trk, FILT_NAME, __VA_ARGS__)

#define FILT_NAME "soundmod.startlevel"

struct startlev {
	ffpcmex fmt;
	double level;
	double val;
	uint64 offset; //number of skipped samples
	void *ni[8];
};

static void* startlev_open(fmed_filt *d)
{
	if (d->audio.fmt.channels > 8)
		return NULL;
	struct startlev *c = ffmem_new(struct startlev);
	if (c == NULL)
		return NULL;
	c->fmt = d->audio.fmt;
	c->level = ffpcm_db2gain(-d->a_start_level);
	return c;
}

static void startlev_close(void *ctx)
{
	struct startlev *c = ctx;
	ffmem_free(c);
}

static int startlev_cb(void *ctx, double val)
{
	struct startlev *c = ctx;
	if (val > c->level) {
		c->val = val;
		return 1;
	}
	return 0;
}

/** Skip audio until the signal level becomes loud enough, and then exit. */
static int startlev_process(void *ctx, fmed_filt *d)
{
	struct startlev *c = ctx;
	size_t samples = d->datalen / ffpcm_size1(&c->fmt);
	ssize_t r = ffpcm_process(&c->fmt, d->data, samples, &startlev_cb, c);
	if (r == -1) {
		c->offset += samples;
		return FMED_RMORE;
	} else if (r < 0) {
		return FMED_RERR;
	}
	c->offset += r;

	double db = ffpcm_gain2db(c->val);
	uint64 tms = ffpcm_time(c->offset, c->fmt.sample_rate);
	infolog(d->trk, "found %.2FdB peak at %u:%02u.%03u (%,U samples)"
		, db, (uint)((tms / 1000) / 60), (uint)((tms / 1000) % 60), (uint)(tms % 1000), c->offset);

	if (c->fmt.ileaved) {
		d->out = (void*)(d->data + r * ffpcm_size1(&c->fmt));
	} else {
		for (uint i = 0;  i != c->fmt.channels;  i++) {
			c->ni[i] = (char*)d->datani[i] + r * ffpcm_bits(c->fmt.format) / 8;
		}
		d->outni = c->ni;
	}
	d->outlen = d->datalen - r * ffpcm_size1(&c->fmt);
	return FMED_RDONE;
}

const struct fmed_filter sndmod_startlev = { startlev_open, startlev_process, startlev_close };

#undef FILT_NAME


#define FILT_NAME "soundmod.stoplevel"

struct stoplev {
	ffpcmex fmt;
	uint state;
	uint all_samples;
	uint max_samples;
	uint min_stop_samples;
	uint nsamples;
	double level;
	double val;
};

#define STOPLEV_DEF_TIME  5000

static void* stoplev_open(fmed_filt *d)
{
	struct stoplev *c = ffmem_new(struct stoplev);
	if (c == NULL)
		return NULL;
	c->fmt = d->audio.fmt;
	c->level = ffpcm_db2gain(-d->a_stop_level);
	uint t = (d->a_stop_level_time != 0) ? d->a_stop_level_time : STOPLEV_DEF_TIME;
	c->max_samples = c->fmt.channels * ffpcm_samples(t, c->fmt.sample_rate);
	c->min_stop_samples = (d->a_stop_level_mintime != 0)
		? c->fmt.channels * ffpcm_samples(d->a_stop_level_mintime, c->fmt.sample_rate)
		: 0;
	return c;
}

static void stoplev_close(void *ctx)
{
	struct stoplev *c = ctx;
	ffmem_free(c);
}

static int stoplev_cb(void *ctx, double val)
{
	struct stoplev *c = ctx;

	c->all_samples++;

	switch (c->state) {
	case 0:
		if (val > c->level)
			break;
		c->val = val;
		c->state = 1;
		//fallthrough

	case 1:
		if (val > c->level) {
			c->nsamples = 0;
			c->state = 0;
			break;
		}
		if (++c->nsamples != c->max_samples)
			break;
		c->state = 2;
		//fallthrough

	case 2:
		if (c->min_stop_samples == 0
			|| c->all_samples == c->min_stop_samples)
			return 1;
		break;
	}
	return 0;
}

static int stoplev_process(void *ctx, fmed_filt *d)
{
	struct stoplev *c = ctx;
	size_t samples = d->datalen / ffpcm_size1(&c->fmt);
	ssize_t r;

	if (!(d->flags & FMED_FFWD))
		return FMED_RMORE;

	r = ffpcm_process(&c->fmt, d->data, samples, &stoplev_cb, c);
	if (r == -1) {
		d->out = d->data;
		d->outlen = d->datalen;
		d->datalen = 0;
		return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RDATA;
	} else if (r < 0) {
		return FMED_RERR;
	}

	double db = ffpcm_gain2db(c->val);
	uint maxsamp = c->max_samples / c->fmt.channels;
	infolog(d->trk, "signal went below %.2FdB level (at %.2FdB) for %ums (%,L samples)"
		, (double)d->a_stop_level, db, (int)ffpcm_time(maxsamp, c->fmt.sample_rate), (size_t)maxsamp);

	d->out = d->data;
	d->outlen = r * ffpcm_size1(&c->fmt);
	return FMED_RLASTOUT;
}

const struct fmed_filter sndmod_stoplev = { stoplev_open, stoplev_process, stoplev_close };

#undef FILT_NAME
