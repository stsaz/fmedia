/** fmedia: zstd filter
2023, Simon Zolin */

#include <fmedia.h>
#include <FFOS/ffos-extern.h>

static const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)

#include <dfilter/zstd-comp.h>
#include <dfilter/zstd-decomp.h>

static const void* fmzstd_iface(const char *name)
{
	if (ffsz_eq(name, "compress"))
		return &fmed_zstdw;
	else if (ffsz_eq(name, "decompress"))
		return &fmed_zstdr;
	return NULL;
}
static int fmzstd_sig(uint signo) { return 0; }
static void fmzstd_destroy(void) {}
static const fmed_mod fmed_zstd = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	fmzstd_iface, fmzstd_sig, fmzstd_destroy, NULL
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_zstd;
}
