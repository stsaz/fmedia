/** MPEG Layer3 (.mp3) reader/writer.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>

extern const fmed_core *core;
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <format/mp3-write.h>
#include <format/mp3-copy.h>
#include <format/mp3-read.h>
