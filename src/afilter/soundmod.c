/** Sound modification.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <afilter/pcm.h>
#include <util/array.h>
#include <util/ring.h>

#define infolog(trk, ...)  fmed_infolog(core, trk, FILT_NAME, __VA_ARGS__)

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

const fmed_core *core;
const fmed_track *track;

#include <afilter/gain.h>
#include <afilter/membuf.h>
#include <afilter/rtpeak.h>
#include <afilter/silence-gen.h>
#include <afilter/until.h>

struct submod {
	const char *name;
	const fmed_filter *iface;
};

extern const fmed_filter fmed_sndmod_conv;
extern const fmed_filter fmed_sndmod_autoconv;
extern const fmed_filter fmed_sndmod_split;
extern const fmed_filter fmed_sndmod_peaks;
extern const fmed_filter sndmod_startlev;
extern const fmed_filter sndmod_stoplev;
extern const fmed_filter fmed_auto_attenuator;
extern const fmed_filter fmed_mix_in;
extern const fmed_filter fmed_mix_out;

static const struct submod submods[] = {
	{ "conv", (fmed_filter*)&fmed_sndmod_conv },
	{ "autoconv", &fmed_sndmod_autoconv },
	{ "gain", &fmed_sndmod_gain },
	{ "until", &fmed_sndmod_until },
	{ "split", &fmed_sndmod_split },
	{ "peaks", &fmed_sndmod_peaks },
	{ "rtpeak", &fmed_sndmod_rtpeak },
	{ "silgen", &sndmod_silgen },
	{ "startlevel", &sndmod_startlev },
	{ "stoplevel", &sndmod_stoplev },
	{ "membuf", &sndmod_membuf },
	{ "auto-attenuator", &fmed_auto_attenuator },
	{ "mixer-in", &fmed_mix_in },
	{ "mixer-out", &fmed_mix_out },
};

static const void* sndmod_iface(const char *name)
{
	const struct submod *m;
	FFARRS_FOREACH(submods, m) {
		if (ffsz_eq(name, m->name))
			return m->iface;
	}
	return NULL;
}

static int sndmod_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		track = core->getmod("#core.track");
		break;
	}
	return 0;
}

static void sndmod_destroy(void)
{
}

int mix_out_conf(fmed_conf_ctx *ctx);

static int sndmod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (ffsz_eq(name, "mixer-out"))
		return mix_out_conf(ctx);
	return -1;
}

static const fmed_mod fmed_sndmod_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	sndmod_iface, sndmod_sig, sndmod_destroy, sndmod_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_sndmod_mod;
}
