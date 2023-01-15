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
#include <format/mmtag.h>

#include <plist/entry.h>
#include <plist/m3u-read.h>

#include <format/flac-read.h>
#include <format/flac-write.h>
#include <format/mp3-copy.h>
#include <format/mp3-read.h>
#include <format/mp4-read.h>
#include <format/mp4-write.h>
#include <format/ogg-read.h>

#include <format/opus-meta.h>
#include <format/vorbis-meta.h>
