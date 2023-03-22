/** SoXr filter.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

static const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)

#include <afilter/soxr-conv.h>

static const void* soxr_mod_iface(const char *name)
{
	if (!ffsz_cmp(name, "conv"))
		return &fmed_soxr;
	return NULL;
}

static int soxr_mod_sig(uint signo)
{
	return 0;
}

static void soxr_mod_destroy(void)
{
}

static const fmed_mod soxr_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	soxr_mod_iface, soxr_mod_sig, soxr_mod_destroy
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &soxr_mod;
}
