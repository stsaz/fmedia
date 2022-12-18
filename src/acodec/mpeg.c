/** MPEG Layer3 decode/encode.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
const fmed_core *core;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <acodec/mpeg-enc.h>
#include <acodec/mpeg-dec.h>

static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &mpeg_decode_filt;
	else if (!ffsz_cmp(name, "encode"))
		return &fmed_mpeg_enc;
	return NULL;
}

static int mpeg_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return mpeg_enc_config(ctx);
	return -1;
}

static int mpeg_sig(uint signo)
{
	return 0;
}

static void mpeg_destroy(void)
{
}

static const fmed_mod fmed_mpeg_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	mpeg_iface, mpeg_sig, mpeg_destroy, mpeg_mod_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpeg_mod;
}
