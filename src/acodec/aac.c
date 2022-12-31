/** AAC input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <acodec/alib3-bridge/aac.h>

const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <acodec/aac-dec.h>
#include <acodec/aac-enc.h>

//FMEDIA MODULE
static const void* aac_iface(const char *name);
static int aac_conf(const char *name, fmed_conf_ctx *ctx);
static int aac_sig(uint signo);
static void aac_destroy(void);
static const fmed_mod fmed_aac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&aac_iface, &aac_sig, &aac_destroy, &aac_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_aac_mod;
}

static const void* aac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &aac_input;
	else if (!ffsz_cmp(name, "encode"))
		return &aac_output;
	return NULL;
}

static int aac_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return aac_out_config(ctx);
	return -1;
}

static int aac_sig(uint signo)
{
	return 0;
}

static void aac_destroy(void)
{
}
