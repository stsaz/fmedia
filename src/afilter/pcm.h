/** PCM.
2015 Simon Zolin */

#pragma once
#include <util/ffos-compat/types.h>
#include <math.h>


enum FFPCM_FMT {
	FFPCM_8 = 8,
	FFPCM_16LE = 16,
	FFPCM_16 = 16,
	FFPCM_24 = 24,
	FFPCM_32 = 32,
	FFPCM_24_4 = 0x0100 | 32,

	_FFPCM_ISFLOAT = 0x0200,
	FFPCM_FLOAT = 0x0200 | 32,
	FFPCM_FLOAT64 = 0x0200 | 64,
};

/** Get format name. */
FF_EXTERN const char* ffpcm_fmtstr(uint fmt);

/** Get format by name. */
FF_EXTERN int ffpcm_fmt(const char *sfmt, size_t len);

typedef struct ffpcm {
	uint format; //enum FFPCM_FMT
	uint channels;
	uint sample_rate;
} ffpcm;

static inline void ffpcm_set(ffpcm *f, ffuint format, ffuint channels, ffuint rate)
{
	f->format = format;
	f->channels = channels;
	f->sample_rate = rate;
}

typedef struct ffpcmex {
	uint format
		, channels
		, sample_rate;
	unsigned ileaved :1;
} ffpcmex;

#define ffpcm_fmtcopy(dst, src) \
do { \
	(dst)->format = (src)->format; \
	(dst)->channels = (src)->channels; \
	(dst)->sample_rate = (src)->sample_rate; \
} while (0)

static inline ffbool ffpcm_eq(const ffpcm *a, const ffpcm *b)
{
	return !ffmem_cmp(a, b, sizeof(*a));
}

static inline ffbool ffpcmex_eq(const ffpcmex *a, const ffpcmex *b)
{
	return !ffmem_cmp(a, b, sizeof(*a));
}

enum {
	FFPCM_CHMASK = 0x0f,
};

/** Channels as string. */
FF_EXTERN const char* ffpcm_channelstr(uint channels);

/** Get channels by name. */
FF_EXTERN int ffpcm_channels(const char *s, size_t len);

/** Get bits per sample for one channel. */
#define ffpcm_bits(fmt)  ((fmt) & 0xff)

/** Get size of 1 sample (in bytes). */
#define ffpcm_size(format, channels)  (ffpcm_bits(format) / 8 * (channels))

#define ffpcm_size1(pcm)  ffpcm_size((pcm)->format, (pcm)->channels)

/** Convert between samples and time. */
#define ffpcm_samples(time_ms, rate)   ((uint64)(time_ms) * (rate) / 1000)
#define ffpcm_time(samples, rate)   ((uint64)(samples) * 1000 / (rate))

/** Convert between bytes and time. */
#define ffpcm_bytes(pcm, time_ms) \
	(ffpcm_samples(time_ms, (pcm)->sample_rate) * ffpcm_size1(pcm))
#define ffpcm_bytes2time(pcm, bytes) \
	ffpcm_time((bytes) / ffpcm_size1(pcm), (pcm)->sample_rate)

/** Protect against division by zero. */
#define FFINT_DIVSAFE(val, by) \
	((by) != 0 ? (val) / (by) : 0)

/** Return bits/sec. */
#define ffpcm_brate(bytes, samples, rate) \
	FFINT_DIVSAFE((uint64)(bytes) * 8 * (rate), samples)

#define ffpcm_brate_ms(bytes, time_ms) \
	FFINT_DIVSAFE((uint64)(bytes) * 8 * 1000, time_ms)

/** Combine two streams together. */
FF_EXTERN void ffpcm_mix(const ffpcmex *pcm, void *stm1, const void *stm2, size_t samples);


/** Convert 16LE sample to FLOAT. */
#define _ffpcm_16le_flt(sh)  ((double)(sh) * (1 / 32768.0))

/** Convert PCM data.
Note: sample rate conversion isn't supported. */
FF_EXTERN int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples);


/** Convert volume knob position to dB value. */
#define ffpcm_vol2db(pos, db_min) \
	(((pos) != 0) ? (log10(pos) * (db_min)/2 /*log10(100)*/ - (db_min)) : -100)

#define ffpcm_vol2db_inc(pos, pos_max, db_max) \
	(pow(10, (double)(pos) / (pos_max)) / 10 * (db_max))

/* gain = 10 ^ (db / 20) */
#define ffpcm_db2gain(db)  pow(10, (double)(db) / 20)
#define ffpcm_gain2db(gain)  (log10(gain) * 20)

FF_EXTERN int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples);


/** Find the highest peak value. */
FF_EXTERN int ffpcm_peak(const ffpcmex *fmt, const void *data, size_t samples, double *maxpeak);

/**
Return 0 to continue;  !=0 to stop. */
typedef int (*ffpcm_process_func)(void *udata, double val);

/** Process PCM data.
Return sample number at which user stopped;
 -1: done;
 <0: error. */
FF_EXTERN ssize_t ffpcm_process(const ffpcmex *fmt, const void *data, size_t samples, ffpcm_process_func func, void *udata);

static FFINL int ffint_ltoh24s(const void *p)
{
	const byte *b = (byte*)p;
	uint n = ((uint)b[2] << 16) | ((uint)b[1] << 8) | b[0];
	if (n & 0x00800000)
		n |= 0xff000000;
	return n;
}

static FFINL void ffint_htol24(void *p, uint n)
{
	byte *o = (byte*)p;
	o[0] = (byte)n;
	o[1] = (byte)(n >> 8);
	o[2] = (byte)(n >> 16);
}
