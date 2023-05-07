/** fmedia/Android: file formats
2022, Simon Zolin */

#include <fmedia.h>
extern const fmed_core *core;
extern const fmed_queue *qu;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <core/format-detector.h>

#include <plist/entry.h>
#include <plist/m3u-read.h>
#include <plist/m3u-write.h>
