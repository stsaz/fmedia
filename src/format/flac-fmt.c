/** fmedia: .flac reader
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>

extern const fmed_core *core;

#undef dbglog
#undef warnlog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define warnlog(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)

#include <format/flac-read.h>
#include <format/flac-write.h>
