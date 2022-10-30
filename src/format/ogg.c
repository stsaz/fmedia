/** OGG input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <util/array.h>
#include <util/path.h>

#define errlog0(...)  fmed_errlog(core, NULL, "ogg", __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

extern const fmed_core *core;

#include <format/ogg-write.h>
#include <format/ogg-read.h>
