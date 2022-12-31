/** fmedia/Android: audio codecs
2022, Simon Zolin */

/*
mpeg.dec->(float/i)
flac.dec->(int8|int16|int24/ni)
(int8|int16|int24/ni)->flac.enc
(int16/i)->aac.enc
*/

#include <fmedia.h>
extern const fmed_core *core;

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

#include <acodec/aac-dec.h>
#include <acodec/flac-dec.h>
#include <acodec/mpeg-dec.h>

#include <acodec/aac-enc.h>
#include <acodec/flac-enc.h>
