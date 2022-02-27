/** MPEG.
2013 Simon Zolin */

#pragma once
#include <afilter/pcm.h>
#include <ffbase/string.h>
#include <mpg123/mpg123-ff.h>

enum FFMPG_E {
	FFMPG_EOK,
	FFMPG_ESYS,
	FFMPG_EFMT,
};

enum FFMPG_R {
	FFMPG_RWARN = -2
	, FFMPG_RERR
	, FFMPG_RHDR
	, FFMPG_RDATA
	, FFMPG_RMORE
	, FFMPG_RDONE
};

/** MPEG decoder. */
typedef struct ffmpg {
	int err;
	mpg123 *m123;
	ffpcmex fmt;
	uint delay_start
		, delay_dec;
	uint64 pos;
	uint64 seek;

	ffstr input;
	size_t pcmlen;
	void *pcmi; //libmpg123: float | short
	uint fin :1;
} ffmpg;

#define ffmpg_input(m, data, len)  ffstr_set(&(m)->input, data, len)

enum FFMPG_DEC_O {
	FFMPG_O_INT16 = 1, //libmpg123: produce 16-bit integer output
};

#define ffmpg_init()  mpg123_init()

/** Get absolute audio position. */
#define ffmpg_pos(m)  ((m)->pos - (m)->pcmlen / ffpcm_size1(&(m)->fmt))

const char* ffmpg_errstr(ffmpg *m)
{
	return mpg123_errstr(m->err);
}

/** Open decoder.
@options: enum FFMPG_DEC_O. */
int ffmpg_open(ffmpg *m, uint delay, uint options)
{
	int r;
	uint opt = (options & FFMPG_O_INT16) ? 0 : MPG123_FORCE_FLOAT;
	if (0 != (r = mpg123_open(&m->m123, opt))) {
		m->err = r;
		return FFMPG_RERR;
	}
	m->fmt.format = (options & FFMPG_O_INT16) ? FFPCM_16 : FFPCM_FLOAT;
	m->fmt.ileaved = 1;
	m->delay_start = delay;
	m->seek = m->delay_start;
	return 0;
}

void ffmpg_close(ffmpg *m)
{
	if (m->m123 != NULL)
		mpg123_free(m->m123);
}

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	mpg123_decode(m->m123, (void*)-1, (size_t)-1, NULL); //reset bufferred data
	m->seek = sample + m->delay_start;
	m->delay_dec = 0;
}

static inline ffuint mpeg1_samples(const void *h)
{
	const ffbyte *b = (ffbyte*)h;
	static const ffbyte frsamps[2][4] = {
		{ 0, 1152/8, 1152/8, 384/8 }, // MPEG-1
		{ 0, 576/8, 1152/8, 384/8 }, // MPEG-2
	};
	int v2 = (b[1] & 0x18) != 0x18;
	int l = (b[1] & 0x06) >> 1;
	return frsamps[v2][l] * 8;
}

/** Decode 1 frame.
Return enum FFMPG_R. */
int ffmpg_decode(ffmpg *m)
{
	int r;

	if (!m->fin && m->input.len == 0)
		return FFMPG_RMORE;

	FF_ASSERT(m->input.len == 0 || m->input.len >= 4);
	r = mpg123_decode(m->m123, m->input.ptr, m->input.len, (byte**)&m->pcmi);
	m->input.len = 0;
	if (r == 0) {
		if (m->input.len != 0)
			m->delay_dec += mpeg1_samples(m->input.ptr);
		return FFMPG_RMORE;

	} else if (r < 0) {
		m->err = r;
		m->delay_dec = 0;
		return FFMPG_RWARN;
	}

	m->pos = ffmax((int64)m->pos - m->delay_dec, 0);

	if (m->seek != (uint64)-1) {
		uint skip = ffmax((int64)(m->seek - m->pos), 0);
		if (skip >= (uint)r / ffpcm_size1(&m->fmt))
			return FFMPG_RMORE;

		m->seek = (uint64)-1;
		m->pcmi = (void*)((char*)m->pcmi + skip * ffpcm_size1(&m->fmt));
		r -= skip * ffpcm_size1(&m->fmt);
		m->pos += skip;
	}

	m->pcmlen = r;
	m->pos += m->pcmlen / ffpcm_size1(&m->fmt) - m->delay_start;
	return FFMPG_RDATA;
}
