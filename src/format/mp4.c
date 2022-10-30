/** MP4 input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>

extern const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <format/mp4-write.h>
#include <format/mp4-read.h>
