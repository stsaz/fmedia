/** ALAC input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

static const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <acodec/alac-dec.h>

//FMEDIA MODULE
static const void* alac_iface(const char *name);
static int alac_sig(uint signo);
static void alac_destroy(void);
static const fmed_mod fmed_alac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&alac_iface, &alac_sig, &alac_destroy
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_alac_mod;
}


static const void* alac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &alac_input;
	return NULL;
}

static int alac_sig(uint signo)
{
	return 0;
}

static void alac_destroy(void)
{
}
