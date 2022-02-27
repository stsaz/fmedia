/** Opus.
2016 Simon Zolin */

/*
OGG(OPUS_HDR)  OGG(OPUS_TAGS)  OGG(OPUS_PKT...)...
*/

#pragma once
#include <afilter/pcm.h>
#include <ffbase/vector.h>
#include <avpack/vorbistag.h>
#include <opus/opus-ff.h>

enum FFOPUS_R {
	FFOPUS_RWARN = -2,
	FFOPUS_RERR = -1,
	FFOPUS_RHDR, //audio info is parsed
	FFOPUS_RTAG, //tag pair is returned
	FFOPUS_RHDRFIN, //header is finished
	FFOPUS_RDATA, //PCM data is returned
	FFOPUS_RMORE,
	FFOPUS_RDONE,
};


#define FFOPUS_HEAD_STR  "OpusHead"

typedef struct ffopus {
	uint state;
	int err;
	opus_ctx *dec;
	struct {
		uint channels;
		uint rate;
		uint orig_rate;
		uint preskip;
	} info;
	uint64 pos;
	ffuint64 dec_pos;
	uint last_decoded;
	ffvec pcmbuf;
	uint64 seek_sample;
	uint64 total_samples;
	int flush;

	vorbistagread vtag;
	int tag;
	ffstr pkt, tagname, tagval;
} ffopus;

#define ffopus_errstr(o)  _ffopus_errstr((o)->err)

static inline void ffopus_seek(ffopus *o, uint64 sample)
{
	o->seek_sample = sample + o->info.preskip;
	o->dec_pos = (ffuint64)-1;
	o->pos = 0;
	o->last_decoded = 0;
}

static inline int ffopus_tag(ffopus *o, ffstr *name, ffstr *val)
{
	*name = o->tagname;
	*val = o->tagval;
	return o->tag;
}

/** Get starting position (sample number) of the last decoded data */
static inline ffuint64 ffopus_startpos(ffopus *o)
{
	return o->dec_pos - o->last_decoded;
}

static inline void ffopus_setpos(ffopus *o, ffuint64 val, int reset)
{
	if (reset) {
		opus_decode_reset(o->dec);
	}
	if (o->dec_pos == (uint64)-1)
		o->dec_pos = val;
	o->pos = val;
	o->last_decoded = 0;
}

#define ffopus_flush(o)  ((o)->flush = 1)

struct opus_hdr {
	char id[8]; //"OpusHead"
	byte ver;
	byte channels;
	byte preskip[2];
	byte orig_sample_rate[4];
	//byte unused[3];
};

#define FFOPUS_TAGS_STR  "OpusTags"

#define ERR(o, n) \
	(o)->err = n,  FFOPUS_RERR

enum {
	FFOPUS_EHDR = 1,
	FFOPUS_EVER,
	FFOPUS_ETAG,

	FFOPUS_ESYS,
};

static const char* const _ffopus_errs[] = {
	"",
	"bad header",
	"unsupported version",
	"invalid tags",
};

const char* _ffopus_errstr(int e)
{
	if (e == FFOPUS_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return _ffopus_errs[e];
	return opus_errstr(e);
}


int ffopus_open(ffopus *o)
{
	o->dec_pos = (ffuint64)-1;
	return 0;
}

void ffopus_close(ffopus *o)
{
	ffvec_free(&o->pcmbuf);
	FF_SAFECLOSE(o->dec, NULL, opus_decode_free);
}

/** Decode Opus packet.
Return enum FFOPUS_R. */
int ffopus_decode(ffopus *o, ffstr *input, ffstr *output)
{
	enum { R_HDR, R_TAGS, R_TAG, R_DATA };
	int r;

	if (input->len == 0 && !o->flush && o->state != R_TAG)
		return FFOPUS_RMORE;

	switch (o->state) {
	case R_HDR: {
		const struct opus_hdr *h = (struct opus_hdr*)input->ptr;
		if (input->len < sizeof(struct opus_hdr)
			|| memcmp(h->id, FFOPUS_HEAD_STR, 8))
			return ERR(o, FFOPUS_EHDR);

		if (h->ver != 1)
			return ERR(o, FFOPUS_EVER);

		o->info.channels = h->channels;
		o->info.rate = 48000;
		o->info.orig_rate = ffint_le_cpu32_ptr(h->orig_sample_rate);
		o->info.preskip = ffint_le_cpu16_ptr(h->preskip);

		opus_conf conf = {0};
		conf.channels = h->channels;
		r = opus_decode_init(&o->dec, &conf);
		if (r != 0)
			return ERR(o, r);

		if (NULL == ffvec_alloc(&o->pcmbuf, OPUS_BUFLEN(o->info.rate) * o->info.channels * sizeof(float), 1))
			return ERR(o, FFOPUS_ESYS);

		o->seek_sample = o->info.preskip;
		o->state = R_TAGS;
		input->len = 0;
		return FFOPUS_RHDR;
	}

	case R_TAGS:
		if (input->len < 8
			|| memcmp(input->ptr, FFOPUS_TAGS_STR, 8)) {
			o->state = R_DATA;
			return FFOPUS_RHDRFIN;
		}
		ffstr_set(&o->pkt, input->ptr + 8, input->len - 8);
		input->len = 0;
		o->state = R_TAG;
		// break

	case R_TAG:
		r = vorbistagread_process(&o->vtag, &o->pkt, &o->tagname, &o->tagval);
		if (r == VORBISTAGREAD_ERROR)
			return ERR(o, FFOPUS_ETAG);
		else if (r == VORBISTAGREAD_DONE) {
			o->state = R_DATA;
			return FFOPUS_RHDRFIN;
		}
		o->tag = r;
		return FFOPUS_RTAG;

	case R_DATA:
		break;
	}

	float *pcm = (void*)o->pcmbuf.ptr;
	r = opus_decode_f(o->dec, input->ptr, input->len, pcm);
	if (r < 0)
		return ERR(o, r);
	input->len = 0;

	if (o->seek_sample != (uint64)-1) {
		if (o->seek_sample > o->pos) {
			uint skip = ffmin(o->seek_sample - o->pos, r);
			o->pos += skip;
			if (o->pos != o->seek_sample || (uint)r == skip)
				return FFOPUS_RMORE; //not yet reached the target packet

			pcm += skip * o->info.channels;
			r -= skip;
		}
		o->seek_sample = (uint64)-1;
	}

	if (o->pos < o->total_samples && o->pos + r >= o->total_samples) {
		r = o->total_samples - o->pos;
	}

	o->dec_pos += r;
	o->last_decoded = r;

	ffstr_set(output, pcm, r * o->info.channels * sizeof(float));
	return FFOPUS_RDATA;
}


typedef struct ffopus_enc {
	uint state;
	opus_ctx *enc;
	uint orig_sample_rate;
	uint bitrate;
	uint sample_rate;
	uint channels;
	uint complexity;
	uint bandwidth;
	int err;
	ffvec buf;
	ffvec bufpcm;
	uint preskip;
	uint packet_dur; //msec

	vorbistagwrite vtag;
	uint min_tagsize;

	ffstr data;

	size_t pcmlen;
	const float *pcm;
	uint64 granulepos;

	uint fin :1;
} ffopus_enc;

#define ffopus_enc_errstr(o)  _ffopus_errstr((o)->err)

/** Add vorbis tag. */
static inline int ffopus_addtag(ffopus_enc *o, const char *name, const char *val, ffsize val_len)
{
	ffstr nm = FFSTR_INITZ(name),  d = FFSTR_INITN(val, val_len);
	return vorbistagwrite_add_name(&o->vtag, nm, d);
}

#define ffopus_enc_pos(o)  ((o)->granulepos)

int ffopus_create(ffopus_enc *o, const ffpcm *fmt, int bitrate)
{
	int r;
	opus_encode_conf conf = {0};
	conf.channels = fmt->channels;
	conf.sample_rate = fmt->sample_rate;
	conf.bitrate = bitrate;
	conf.complexity = o->complexity;
	conf.bandwidth = o->bandwidth;
	if (0 != (r = opus_encode_create(&o->enc, &conf)))
		return ERR(o, r);
	o->preskip = conf.preskip;

	if (NULL == ffvec_alloc(&o->buf, OPUS_MAX_PKT, 1))
		return ERR(o, FFOPUS_ESYS);

	const char *svendor = opus_vendor();
	ffstr vendor = FFSTR_INITZ(svendor);
	vorbistagwrite_add(&o->vtag, MMTAG_VENDOR, vendor);

	if (o->packet_dur == 0)
		o->packet_dur = 40;
	o->channels = fmt->channels;
	o->sample_rate = fmt->sample_rate;
	o->bitrate = bitrate;
	return 0;
}

void ffopus_enc_close(ffopus_enc *o)
{
	vorbistagwrite_destroy(&o->vtag);
	ffvec_free(&o->buf);
	ffvec_free(&o->bufpcm);
	opus_encode_free(o->enc);
}

/** Get approximate output file size. */
uint64 ffopus_enc_size(ffopus_enc *o, uint64 total_samples)
{
	return (total_samples / o->sample_rate + 1) * (o->bitrate / 8);
}

/** Get complete packet with Vorbis comments and padding. */
static int _ffopus_tags(ffopus_enc *o, ffstr *pkt)
{
	ffstr tags = vorbistagwrite_fin(&o->vtag);
	uint npadding = (tags.len < o->min_tagsize) ? o->min_tagsize - tags.len : 0;

	o->buf.len = 0;
	if (NULL == ffvec_realloc((ffvec*)&o->buf, FFS_LEN(FFOPUS_TAGS_STR) + tags.len + npadding, 1))
		return FFOPUS_ESYS;

	char *d = o->buf.ptr;
	ffmem_copy(d, FFOPUS_TAGS_STR, FFS_LEN(FFOPUS_TAGS_STR));
	ffsize i = FFS_LEN(FFOPUS_TAGS_STR);

	ffmem_copy(&d[i], tags.ptr, tags.len);
	i += tags.len;

	ffmem_zero(&d[i], npadding);
	o->buf.len = i + npadding;

	ffstr_setstr(pkt, &o->buf);
	return 0;
}

/**
Return enum FFVORBIS_R. */
int ffopus_encode(ffopus_enc *o)
{
	enum { W_HDR, W_TAGS, W_DATA1, W_DATA, W_DONE };
	int r;

	switch (o->state) {
	case W_HDR: {
		struct opus_hdr *h = (void*)o->buf.ptr;
		ffmemcpy(h->id, FFOPUS_HEAD_STR, 8);
		h->ver = 1;
		h->channels = o->channels;
		*(uint*)h->orig_sample_rate = ffint_le_cpu32(o->orig_sample_rate);
		*(ushort*)h->preskip = ffint_le_cpu16(o->preskip);
		ffmem_zero(h + 1, 3);
		ffstr_set(&o->data, o->buf.ptr, sizeof(struct opus_hdr) + 3);
		o->state = W_TAGS;
		return FFOPUS_RDATA;
	}

	case W_TAGS:
		if (0 != (r = _ffopus_tags(o, &o->data)))
			return ERR(o, r);
		o->state = W_DATA1;
		return FFOPUS_RDATA;

	case W_DATA1: {
		uint padding = o->preskip * ffpcm_size(FFPCM_FLOAT, o->channels);
		if (NULL == ffvec_grow(&o->bufpcm, padding, 1))
			return ERR(o, FFOPUS_ESYS);
		ffmem_zero(o->bufpcm.ptr + o->bufpcm.len, padding);
		o->bufpcm.len = padding;
		o->state = W_DATA;
		// break
	}

	case W_DATA:
		break;

	case W_DONE:
		o->data.len = 0;
		return FFOPUS_RDONE;
	}

	uint samp_size = ffpcm_size(FFPCM_FLOAT, o->channels);
	uint fr_samples = ffpcm_samples(o->packet_dur, 48000);
	uint fr_size = fr_samples * samp_size;
	uint samples = fr_samples;
	ffstr d;
	r = ffstr_gather((ffstr*)&o->bufpcm, &o->bufpcm.cap, (void*)o->pcm, o->pcmlen, fr_size, &d);
	if (r < 0)
		return ERR(o, FFOPUS_ESYS);
	o->pcmlen -= r;
	o->pcm = (void*)((char*)o->pcm + r);
	if (d.len == 0) {
		if (!o->fin) {
			o->pcmlen = 0;
			return FFOPUS_RMORE;
		}
		uint padding = fr_size - o->bufpcm.len;
		samples = o->bufpcm.len / samp_size;
		if (NULL == ffvec_grow(&o->bufpcm, padding, 1))
			return ERR(o, FFOPUS_ESYS);
		ffmem_zero(o->bufpcm.ptr + o->bufpcm.len, padding);
		d.ptr = o->bufpcm.ptr;
		o->state = W_DONE;
	}
	o->bufpcm.len = 0;

	r = opus_encode_f(o->enc, (void*)d.ptr, fr_samples, o->buf.ptr);
	if (r < 0)
		return ERR(o, r);
	o->granulepos += samples;
	ffstr_set(&o->data, o->buf.ptr, r);
	return FFOPUS_RDATA;
}
