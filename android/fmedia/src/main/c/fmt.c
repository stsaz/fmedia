/** fmedia/Android: file formats
2022, Simon Zolin */

#include <fmedia.h>
extern const fmed_core *core;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <format/mmtag.h>
#include <format/mp3-read.h>
#include <format/mp4-read.h>
