/** MPEG Layer3 decode/encode.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <util/array.h>
#include <mpg123/mpg123-ff.h>

const fmed_core *core;

#include <acodec/mpeg-write.h>

const fmed_filter mpeg_decode_filt;

static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &mpeg_decode_filt;
	else if (!ffsz_cmp(name, "encode"))
		return &fmed_mpeg_enc;
	return NULL;
}

static int mpeg_mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return mpeg_enc_config(ctx);
	return -1;
}

static int mpeg_sig(uint signo)
{
	return 0;
}

static void mpeg_destroy(void)
{
}

static const fmed_mod fmed_mpeg_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	mpeg_iface, mpeg_sig, mpeg_destroy, mpeg_mod_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpeg_mod;
}


typedef struct mpeg_dec {
	mpg123 *m123;
	uint64 pos;
	uint64 seek;
	uint fr_size;
	uint sample_rate;
} mpeg_dec;

static void mpeg_dec_close(void *ctx);

static void* mpeg_dec_open(fmed_filt *d)
{
	mpeg_dec *m;
	if (NULL == (m = ffmem_tcalloc1(mpeg_dec)))
		return NULL;

	mpg123_init();

	int err;
	if (0 != (err = mpg123_open(&m->m123, MPG123_FORCE_FLOAT))) {
		mpeg_dec_close(m);
		return NULL;
	}

	m->seek = (uint64)-1;
	if (d->mpeg1_delay != 0)
		m->seek = d->mpeg1_delay;

	d->audio.fmt.channels = d->audio.fmt.channels;
	d->audio.fmt.format = FFPCM_FLOAT;
	d->audio.fmt.ileaved = 1;
	m->fr_size = ffpcm_size1(&d->audio.fmt);
	m->sample_rate = d->audio.fmt.sample_rate;
	d->datatype = "pcm";
	return m;
}

static void mpeg_dec_close(void *ctx)
{
	mpeg_dec *m = ctx;
	if (m->m123 != NULL)
		mpg123_free(m->m123);
	ffmem_free(m);
}

static int mpeg_dec_process(void *ctx, fmed_filt *d)
{
	mpeg_dec *m = ctx;
	int r = 0;
	ffstr in = {}, out = {};

	if (d->flags & FMED_FFWD) {
		in = d->data_in;
		d->data_in.len = 0;
		m->pos = d->audio.pos;

		if ((int64)d->audio.seek != FMED_NULL) {
			mpg123_reset(m->m123);
			m->seek = ffpcm_samples(d->audio.seek, m->sample_rate) + d->mpeg1_delay;
			d->audio.seek = FMED_NULL;
		}
	}

	if (in.len != 0 || (d->flags & FMED_FLAST))
		r = mpg123_decode(m->m123, in.ptr, in.len, (byte**)&out.ptr);

	if (r == 0) {
		goto end;
	} else if (r < 0) {
		errlog(core, d->trk, "mpeg", "mpg123_decode(): %s. Near sample %U"
			, mpg123_errstr(r), d->audio.pos);
		goto end;
	}

	ffstr_set(&d->data_out, out.ptr, r);
	d->audio.pos = m->pos - d->mpeg1_delay;

	uint samples = r / m->fr_size;
	if (m->seek != (uint64)-1) {
		if (m->pos + samples <= m->seek) {
			m->pos += samples;
			goto end;
		}
		if (m->pos < m->seek) {
			int64 skip_samples = (int64)m->seek - m->pos;
			FF_ASSERT(skip_samples >= 0);
			d->audio.pos += skip_samples;
			ffstr_shift(&d->data_out, skip_samples * m->fr_size);
			dbglog(core, d->trk, "mpeg", "skip %L samples", skip_samples);
		}
		m->seek = (uint64)-1;
	}

	m->pos += samples;

	// skip padding
	if (d->audio.total != 0 && d->mpeg1_padding != 0
		&& d->audio.pos < d->audio.total
		&& d->audio.pos + d->data_out.len / m->fr_size > d->audio.total) {

		uint n = d->audio.pos + d->data_out.len / m->fr_size - d->audio.total;
		n = ffmin(n, d->mpeg1_padding);
		dbglog(core, d->trk, "mpeg", "cut last %u samples", n);
		d->data_out.len -= n * m->fr_size;
	}

	dbglog(core, d->trk, "mpeg", "output: %u PCM samples"
		, samples);
	return FMED_RDATA;

end:
	d->data_out.len = 0;
	return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RMORE;
}

const fmed_filter mpeg_decode_filt = {
	mpeg_dec_open, mpeg_dec_process, mpeg_dec_close
};
