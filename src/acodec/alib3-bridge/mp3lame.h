/** libmp3lame wrapper.
2015 Simon Zolin */

#pragma once
#include <ffbase/vector.h>
#include <mp3lame/lame-ff.h>

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

typedef struct ffmpg_enc {
	uint state;
	int err;
	lame *lam;
	ffpcm fmt;
	uint qual;

	size_t pcmlen;
	union {
	const short **pcm;
	const float **pcmf;
	const short *pcmi;
	};
	size_t pcmoff;
	uint samp_size;

	ffvec buf;
	size_t datalen;
	const void *data;
	uint off;

	uint fin :1
		, ileaved :1
		;
	uint options; //enum FFMPG_ENC_OPT
} ffmpg_enc;

const char* ffmpg_enc_errstr(ffmpg_enc *m)
{
	switch (m->err) {
	case FFMPG_ESYS:
		return fferr_strp(fferr_last());

	case FFMPG_EFMT:
		return "PCM format error";
	}

	return lame_errstr(m->err);
}

/**
@qual: 9..0(better) for VBR or 10..320 for CBR
Return enum FFMPG_E. */
int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual)
{
	int r;

	switch (pcm->format) {
	case FFPCM_16:
		break;

	case FFPCM_FLOAT:
		if (!m->ileaved)
			break;
		// break

	default:
		pcm->format = FFPCM_16;
		m->err = FFMPG_EFMT;
		return FFMPG_EFMT;
	}

	lame_params conf = {0};
	conf.format = ffpcm_bits(pcm->format);
	conf.interleaved = m->ileaved;
	if (pcm->channels == 1)
		conf.interleaved = 0;
	conf.channels = pcm->channels;
	conf.rate = pcm->sample_rate;
	conf.quality = qual;
	if (0 != (r = lame_create(&m->lam, &conf))) {
		m->err = r;
		return FFMPG_EFMT;
	}

	if (NULL == ffvec_realloc(&m->buf, 125 * (8 * 1152) / 100 + 7200, 1)) {
		m->err = FFMPG_ESYS;
		return FFMPG_ESYS;
	}

	m->fmt = *pcm;
	m->samp_size = ffpcm_size1(pcm);
	m->qual = qual;
	return FFMPG_EOK;
}

void ffmpg_enc_close(ffmpg_enc *m)
{
	ffvec_free(&m->buf);
	FF_SAFECLOSE(m->lam, NULL, lame_free);
}

/** Get approximate output file size. */
uint64 ffmpg_enc_size(ffmpg_enc *m, uint64 total_samples)
{
	static const byte vbitrate[] = {
		245, 225, 190, 175, 165, 130, 115, 100, 85, 65 //q=0..9 for 44.1kHz stereo
	};

	uint kbrate = (m->qual <= 9) ? vbitrate[m->qual] * m->fmt.channels / 2 : m->qual;
	return (total_samples / m->fmt.sample_rate + 1) * (kbrate * 1000 / 8);
}

/**
Return enum FFMPG_R. */
int ffmpg_encode(ffmpg_enc *m)
{
	enum { I_DATA, I_LAMETAG };
	size_t nsamples;
	int r = 0;

	switch (m->state) {
	case I_LAMETAG:
		r = lame_lametag(m->lam, m->buf.ptr, m->buf.cap);
		m->data = m->buf.ptr;
		m->datalen = ((uint)r <= m->buf.cap) ? r : 0;
		return FFMPG_RDONE;
	}

	for (;;) {

	nsamples = m->pcmlen / m->samp_size;
	nsamples = ffmin(nsamples, 8 * 1152);

	r = 0;
	if (nsamples != 0) {
		const void *pcm[2];
		if (m->ileaved)
			pcm[0] = (char*)m->pcmi + m->pcmoff * m->fmt.channels;
		else {
			for (uint i = 0;  i != m->fmt.channels;  i++) {
				pcm[i] = (char*)m->pcm[i] + m->pcmoff;
			}
		}
		r = lame_encode(m->lam, pcm, nsamples, m->buf.ptr, m->buf.cap);
		if (r < 0) {
			m->err = r;
			return FFMPG_RERR;
		}
		m->pcmoff += nsamples * ffpcm_bits(m->fmt.format)/8;
		m->pcmlen -= nsamples * m->samp_size;
	}

	if (r == 0) {
		if (m->pcmlen != 0)
			continue;

		if (!m->fin) {
			m->pcmoff = 0;
			return FFMPG_RMORE;
		}

		r = lame_encode(m->lam, NULL, 0, (char*)m->buf.ptr, m->buf.cap);
		if (r < 0) {
			m->err = r;
			return FFMPG_RERR;
		}
		m->state = I_LAMETAG;
	}

	m->data = m->buf.ptr;
	m->datalen = r;
	return FFMPG_RDATA;
	}
}
