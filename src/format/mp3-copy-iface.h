/** MPEG.
Copyright (c) 2013 Simon Zolin
*/

/*
MPEG-HEADER  [CRC16]  ([XING-TAG  LAME-TAG]  |  FRAME-DATA...) ...
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <avpack/mpeg1-read.h>
#include <avpack/mp3-write.h>


enum FFMPG_R {
	FFMPG_RWARN = -2,
	FFMPG_RERR,
	FFMPG_RHDR,
	FFMPG_RDATA,
	FFMPG_RFRAME,
	FFMPG_RMORE,
	FFMPG_RSEEK,
	FFMPG_RDONE,
	FFMPG_RID32,
	FFMPG_RID31,
	FFMPG_RAPETAG,
	FFMPG_ROUTSEEK,
};

typedef struct ffmpgcopy {
	uint state;
	uint options; //enum FFMPG_ENC_OPT
	mpeg1read rdr;
	mp3write writer;
	uint wflags;
	uint gstate;
	uint gsize;
	uint wdataoff;
	uint64 off;
	char id31[128];
	ffarr buf;
	ffstr rinput;
	const char *error;
	ffuint64 total_size;
} ffmpgcopy;

enum {
	CPY_START, CPY_GATHER,
	CPY_ID32_HDR, CPY_ID32_DATA,
	CPY_DATA, CPY_FR1, CPY_FR, CPY_FR_OUT,
	CPY_FTRTAGS_SEEK, CPY_FTRTAGS, CPY_FTRTAGS_OUT,
};

static inline void ffmpg_copy_close(ffmpgcopy *m)
{
	ffarr_free(&m->buf);
	mpeg1read_close(&m->rdr);
	mp3write_close(&m->writer);
}

#define ffmpg_copy_errstr(m)  (m)->error
#define ffmpg_copy_fmt(m)  ffmpg_fmt(&(m)->rdr)
#define ffmpg_copy_seekoff(m)  ((m)->off)
static inline void ffmpg_copy_seek(ffmpgcopy *m, uint64 sample)
{
	mpeg1read_seek(&m->rdr, sample);
}
static inline void ffmpg_copy_fin(ffmpgcopy *m)
{
	m->state = CPY_FTRTAGS_OUT;
}
#define ffmpg_copy_setsize(m, size)  (m)->total_size = (size)

/** Return TRUE if valid ID3v1 header. */
static inline ffbool ffid31_valid(const char *h)
{
	return h[0] == 'T' && h[1] == 'A' && h[2] == 'G';
}

typedef struct ffid3_hdr {
	char id3[3]; //"ID3"
	byte ver[2]; //e.g. \3 \0
	byte flags; //enum FFID3_FHDR
	byte size[4]; //7-bit numbers
} ffid3_hdr;

static inline ffbool ffid3_valid(const ffid3_hdr *h)
{
	const uint *hsize = (void*)h->size;
	return h->id3[0] == 'I' && h->id3[1] == 'D' && h->id3[2] == '3'
		&& h->ver[0] != 0xff && h->ver[1] != 0xff
		&& (*hsize & 0x80808080) == 0;
}

static inline uint i28_i32(const void *src)
{
	uint i = ffint_be_cpu32_ptr(src);
	return ((i & 0x7f000000) >> 3)
		| ((i & 0x007f0000) >> 2)
		| ((i & 0x00007f00) >> 1)
		| (i & 0x0000007f);
}

static inline uint ffid3_size(const ffid3_hdr *h)
{
	return i28_i32(h->size);
}

/** Copy tags and frames. */
/* MPEG copy:
. Read and return ID3v2 (FFMPG_RID32)
. Read the first frame (FFMPG_RHDR)
  User may call ffmpg_copy_seek() now.
. If input is seekable:
  . Seek input to the end (FFMPG_RSEEK), read ID3v1
  . Seek input to the needed audio position (FFMPG_RSEEK)
. Return empty Xing frame (FFMPG_RFRAME)
. Read and return MPEG frames (FFMPG_RFRAME) until:
  . User calls ffmpg_copy_fin()
  . Or the end of audio data is reached
. Return ID3v1 (FFMPG_RID31)
. Seek output to Xing tag offset (FFMPG_ROUTSEEK)
. Write the complete Xing tag (FFMPG_RFRAME)
*/
static inline int ffmpg_copy(ffmpgcopy *m, ffstr *input, ffstr *output)
{
	const int MPG_FTRTAGS_CHKSIZE = 1000; //number of bytes at the end of file to check for ID3v1 and APE tag (initial check)

	int r;
	ffstr fr = {};

	for (;;) {
	switch (m->state) {

	case CPY_START:
		mp3write_create(&m->writer);
		m->writer.options = MP3WRITE_XINGTAG;

		if (m->options & MP3WRITE_ID3V2) {
			m->gsize = sizeof(ffid3_hdr);
			m->state = CPY_GATHER,  m->gstate = CPY_ID32_HDR;
			continue;
		}
		m->state = CPY_DATA,  m->gstate = CPY_FR1;
		continue;

	case CPY_GATHER:
		r = ffarr_append_until(&m->buf, input->ptr, input->len, m->gsize);
		switch (r) {
		case 0:
			return FFMPG_RMORE;
		case -1:
			return m->error = "no mem",  FFMPG_RERR;
		}
		ffstr_shift(input, r);
		m->state = m->gstate;
		continue;


	case CPY_ID32_HDR:
		if (ffid3_valid((void*)m->buf.ptr)) {
			m->gsize = ffid3_size((void*)m->buf.ptr);
			m->wdataoff = sizeof(ffid3_hdr) + m->gsize;
			m->state = CPY_ID32_DATA;
			ffstr_set2(output, &m->buf);
			m->buf.len = 0;
			return FFMPG_RID32;
		}

		ffstr_set(&m->rinput, m->buf.ptr, m->buf.len);
		m->buf.len = 0;
		m->state = CPY_FR1;
		continue;

	case CPY_ID32_DATA:
		if (m->gsize == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR1;
			continue;
		}

		if (input->len == 0)
			return FFMPG_RMORE;

		r = ffmin(input->len, m->gsize);
		m->gsize -= r;
		ffstr_set(output, input->ptr, r);
		ffstr_shift(input, r);
		return FFMPG_RID32;


	case CPY_FTRTAGS_SEEK:
		if (m->total_size == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR;
			continue;
		}

		m->gsize = ffmin64(MPG_FTRTAGS_CHKSIZE, m->total_size);
		m->state = CPY_GATHER,  m->gstate = CPY_FTRTAGS;
		m->off = ffmin64(m->total_size - MPG_FTRTAGS_CHKSIZE, m->total_size);
		return FFMPG_RSEEK;

	case CPY_FTRTAGS: {
		const void *h = m->buf.ptr + m->buf.len - 128;
		if (m->buf.len >= 128 && ffid31_valid(h)) {
			if (m->options & MP3WRITE_ID3V1)
				ffmemcpy(m->id31, h, 128);
			m->total_size -= 128;
		}

		m->state = CPY_DATA,  m->gstate = CPY_FR;
		m->buf.len = 0;
		m->off = m->wdataoff + mpeg1read_offset(&m->rdr);
		return FFMPG_RSEEK;
	}

	case CPY_FTRTAGS_OUT:
		m->wflags = MP3WRITE_FLAST;
		m->state = CPY_FR_OUT;
		if (m->id31[0] != '\0') {
			ffstr_set(output, m->id31, 128);
			return FFMPG_RID31;
		}
		continue;


	case CPY_DATA:
		if (input->len == 0)
			return FFMPG_RMORE;
		ffstr_set(&m->rinput, input->ptr, input->len);
		input->len = 0;
		m->state = m->gstate;
		continue;

	case CPY_FR1:
		mpeg1read_open(&m->rdr, m->total_size);
		// fallhrough
	case CPY_FR:
		r = mpeg1read_process(&m->rdr, &m->rinput, &fr);
		switch (r) {

		case MPEG1READ_MORE:
			m->gstate = m->state;
			m->state = CPY_DATA;
			continue;

		case MPEG1READ_SEEK:
			m->off = m->wdataoff + mpeg1read_offset(&m->rdr);
			m->gstate = m->state;
			m->state = CPY_DATA;
			return FFMPG_RSEEK;

		case MP3READ_DONE:
			m->state = CPY_FTRTAGS_OUT;
			continue;

		case MPEG1READ_HEADER:
			m->writer.vbr_scale = mpeg1read_info(&m->rdr)->vbr_scale;
			m->state = CPY_FTRTAGS_SEEK;
			return FFMPG_RHDR;

		case MPEG1READ_DATA:
			m->state = CPY_FR_OUT;
			continue;
		case MPEG1READ_ERROR:
			m->error = mpeg1read_error(&m->rdr);
			return FFMPG_RERR;
		}
		FF_ASSERT(0);
		return FFMPG_RERR;

	case CPY_FR_OUT:
		r = mp3write_process(&m->writer, &fr, output, m->wflags);
		switch (r) {
		case MP3WRITE_MORE:
			break;

		case MP3WRITE_DONE:
			return FFMPG_RDONE;

		case MP3WRITE_DATA:
			if (!(m->wflags & MP3WRITE_FLAST))
				m->state = CPY_FR;
			return FFMPG_RFRAME;

		case MP3WRITE_ERROR:
			m->error = "mp3write_process error";
			return FFMPG_RERR;

		case MP3WRITE_SEEK:
			m->off = m->wdataoff + mp3write_offset(&m->writer);
			return FFMPG_ROUTSEEK;
		}
		FF_ASSERT(0);
		return FFMPG_RERR;

	}
	}
}
