/** Terminal UI.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>


typedef struct tui {
	uint state;
	uint64 total_samples;
	uint lastpos;
	uint sample_rate;
	uint total_time_sec;
	ffstr3 buf;
} tui;

static const fmed_core *core;

//FMEDIA MODULE
static const void* tui_iface(const char *name);
static int tui_sig(uint signo);
static void tui_destroy(void);
static const fmed_mod fmed_tui_mod = {
	&tui_iface, &tui_sig, &tui_destroy
};

static void* tui_open(fmed_filt *d);
static int tui_process(void *ctx, fmed_filt *d);
static void tui_close(void *ctx);
static const fmed_filter fmed_tui = {
	&tui_open, &tui_process, &tui_close
};

static void tui_info(tui *t, fmed_filt *d);


const fmed_mod* fmed_getmod_tui(const fmed_core *_core)
{
	core = _core;
	return &fmed_tui_mod;
}


static const void* tui_iface(const char *name)
{
	if (!ffsz_cmp(name, "tui"))
		return &fmed_tui;
	return NULL;
}

static int tui_sig(uint signo)
{
	return 0;
}

static void tui_destroy(void)
{
}


static void* tui_open(fmed_filt *d)
{
	tui *t = ffmem_tcalloc1(tui);
	if (t == NULL)
		return NULL;

	t->total_samples = d->track->getval(d->trk, "total_samples");
	t->sample_rate = (int)d->track->getval(d->trk, "pcm_sample_rate");
	tui_info(t, d);

	if (FMED_NULL != d->track->getval(d->trk, "input_info")) {
		tui_close(t);
		return NULL;
	}

	return t;
}

static void tui_close(void *ctx)
{
	tui *t = ctx;
	ffarr_free(&t->buf);
	ffmem_free(t);
}

static void tui_info(tui *t, fmed_filt *d)
{
	uint64 total_time, tsize;
	uint tmsec;
	const char *artist, *title;

	total_time = (t->total_samples != FMED_NULL) ? ffpcm_time(t->total_samples, t->sample_rate) : 0;
	tmsec = (uint)(total_time / 1000);
	t->total_time_sec = tmsec;

	tsize = d->track->getval(d->trk, "total_size");
	if (tsize == FMED_NULL)
		tsize = 0;

	artist = d->track->getvalstr(d->trk, "meta_artist");
	if (artist == FMED_PNULL)
		artist = "";
	title = d->track->getvalstr(d->trk, "meta_title");
	if (title == FMED_PNULL)
		title = "";

	t->buf.len = 0;
	ffstr_catfmt(&t->buf, "\"%s - %s\" %s %U.%02u MB, %u:%02u.%03u, %u kbps, %u Hz, %u bit, %s\n\n"
		, artist, title
		, d->track->getvalstr(d->trk, "input")
		, tsize / (1024 * 1024), (uint)((tsize % (1024 * 1024) * 100) / (1024 * 1024))
		, tmsec / 60, tmsec % 60, (uint)(total_time % 1000)
		, (int)(d->track->getval(d->trk, "bitrate") / 1000)
		, t->sample_rate
		, (int)ffpcm_bits[d->track->getval(d->trk, "pcm_format")]
		, ffpcm_channelstr((int)d->track->getval(d->trk, "pcm_channels")));
	ffstd_write(ffstderr, t->buf.ptr, t->buf.len);
	t->buf.len = 0;
}

static int tui_process(void *ctx, fmed_filt *d)
{
	tui *t = ctx;
	int64 playpos;
	uint playtime;
	uint dots = 70;
	uint nback = (uint)t->buf.len;

	playpos = fmed_getval("current_position");
	if (t->total_samples == FMED_NULL
		|| playpos == FMED_NULL
		|| (uint64)playpos > t->total_samples) {
		d->out = d->data;
		d->outlen = d->datalen;
		return FMED_RDONE;
	}

	if (core->loglev & FMED_LOG_DEBUG)
		nback = 0;

	playtime = (uint)(ffpcm_time(playpos, t->sample_rate) / 1000);
	if (playtime == t->lastpos)
		goto done;
	t->lastpos = playtime;
	fffile_fmt(ffstderr, &t->buf, "%*c[%*c%*c] %u:%02u / %u:%02u"
		, (size_t)nback, '\b'
		, (size_t)(playpos * dots / t->total_samples), '='
		, (size_t)(dots - (playpos * dots / t->total_samples)), '.'
		, playtime / 60, playtime % 60
		, t->total_time_sec / 60, t->total_time_sec % 60);
	t->buf.len -= nback; //don't count the number of '\b'

done:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}
