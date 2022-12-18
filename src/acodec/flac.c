/** FLAC input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

const fmed_core *core;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <acodec/flac-dec.h>
#include <acodec/flac-enc.h>

//FMEDIA MODULE
static const void* flac_iface(const char *name);
static int flac_mod_conf(const char *name, fmed_conf_ctx *conf);
static int flac_sig(uint signo);
static void flac_destroy(void);
static const fmed_mod fmed_flac_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&flac_iface, &flac_sig, &flac_destroy, &flac_mod_conf
};



FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_flac_mod;
}


static const void* flac_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_flac_dec;
	else if (!ffsz_cmp(name, "encode"))
		return &mod_flac_enc;
	return NULL;
}

static int flac_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return flac_enc_config(ctx);
	return -1;
}

static int flac_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		break;
	}
	return 0;
}

static void flac_destroy(void)
{
}
