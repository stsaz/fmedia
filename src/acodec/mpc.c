/** Musepack input.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/mpc.h>
#include <FF/audio/musepack.h>
#include <FF/mtags/mmtag.h>


static const fmed_core *core;

//FMEDIA MODULE
static const void* mpc_iface(const char *name);
static int mpc_mod_conf(const char *name, ffpars_ctx *ctx);
static int mpc_sig(uint signo);
static void mpc_destroy(void);
static const fmed_mod fmed_mpc_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mpc_iface, &mpc_sig, &mpc_destroy, &mpc_mod_conf
};

//INPUT
static void* mpc_open(fmed_filt *d);
static void mpc_close(void *ctx);
static int mpc_process(void *ctx, fmed_filt *d);
static const fmed_filter mpc_input = {
	&mpc_open, &mpc_process, &mpc_close
};

typedef struct mpc {
	ffmpcr mpc;
	int64 aseek;
	uint seeking :1;
} mpc;

//DECODE
static void* mpc_dec_open(fmed_filt *d);
static void mpc_dec_close(void *ctx);
static int mpc_dec_process(void *ctx, fmed_filt *d);
static const fmed_filter mpc_decoder = {
	&mpc_dec_open, &mpc_dec_process, &mpc_dec_close
};

typedef struct mpcdec {
	ffmpc mpcdec;
	ffpcm fmt;
} mpcdec;


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpc_mod;
}


static const void* mpc_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &mpc_input;
	if (!ffsz_cmp(name, "decode"))
		return &mpc_decoder;
	return NULL;
}

static int mpc_mod_conf(const char *name, ffpars_ctx *ctx)
{
	return -1;
}

static int mpc_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;
	}
	return 0;
}

static void mpc_destroy(void)
{
}


static void* mpc_open(fmed_filt *d)
{
	mpc *m;
	if (NULL == (m = ffmem_new(mpc)))
		return NULL;
	m->aseek = -1;
	ffmpc_ropen(&m->mpc);
	m->mpc.options = FFMPC_O_APETAG | FFMPC_O_SEEKTABLE;

	if ((int64)d->input.size != FMED_NULL) {
		ffmpc_setsize(&m->mpc, d->input.size);
	}

	return m;
}

static void mpc_close(void *ctx)
{
	mpc *m = ctx;
	ffmpc_rclose(&m->mpc);
	ffmem_free(m);
}

static int mpc_process(void *ctx, fmed_filt *d)
{
	mpc *m = ctx;
	int r;
	ffstr blk;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->codec_err) {
		d->codec_err = 0;
		ffmpc_streamerr(&m->mpc);
	}

	if (d->datalen != 0) {
		ffmpc_input(&m->mpc, d->data, d->datalen);
		d->datalen = 0;
	}

	if ((int64)d->audio.seek >= 0)
		m->aseek = d->audio.seek;
	if (m->aseek >= 0 && !m->seeking && m->mpc.sample_rate != 0) {
		ffmpc_blockseek(&m->mpc, ffpcm_samples(m->aseek, m->mpc.sample_rate));
		m->seeking = 1;
	}

	for (;;) {
		r = ffmpc_read(&m->mpc);

		switch (r) {

		case FFMPC_RHDR:
			d->audio.fmt.sample_rate = m->mpc.sample_rate;
			d->audio.fmt.channels = m->mpc.channels;
			d->audio.bitrate = ffmpc_bitrate(&m->mpc);
			d->audio.total = ffmpc_length(&m->mpc);
			d->audio.decoder = "Musepack";

			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpc.decode"))
				return FMED_RERR;

			d->out = m->mpc.sh_block,  d->outlen = m->mpc.sh_block_len;
			return FMED_RDATA;

		case FFMPC_RMORE:
			return FMED_RMORE;

		case FFMPC_RSEEK:
			d->input.seek = ffmpc_off(&m->mpc);
			return FMED_RMORE;

		case FFMPC_RTAG: {
			ffstr name, val;
			name = m->mpc.apetag.name;
			if (FFAPETAG_FBINARY == (m->mpc.apetag.flags & FFAPETAG_FMASK)) {
				dbglog(core, d->trk, NULL, "skipping binary tag: %S", &m->mpc.apetag.name);
				continue;
			}

			if (m->mpc.tag != 0)
				ffstr_setz(&name, ffmmtag_str[m->mpc.tag]);

			ffstr_set2(&val, &m->mpc.tagval);

			dbglog(core, d->trk, NULL, "tag: %S: %S", &name, &val);
			d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
			continue;
		}

		case FFMPC_RBLOCK:
			goto data;

		case FFMPC_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMPC_RWARN:
		case FFMPC_RERR:
			errlog(core, d->trk, "mpc", "ffmpc_read(): %s.  Offset: %U"
				, ffmpc_rerrstr(&m->mpc), ffmpc_off(&m->mpc));
			return FMED_RERR;
		}
	}

data:
	if (m->seeking) {
		m->seeking = 0;
		m->aseek = -1;
	}
	ffmpc_blockdata(&m->mpc, &blk);
	d->audio.pos = ffmpc_blockpos(&m->mpc);
	dbglog(core, d->trk, NULL, "passing %L bytes at position #%U"
		, blk.len, d->audio.pos);
	d->out = blk.ptr,  d->outlen = blk.len;
	return FMED_RDATA;
}


static void* mpc_dec_open(fmed_filt *d)
{
	mpcdec *m;
	if (NULL == (m = ffmem_new(mpcdec)))
		return NULL;
	if (0 != ffmpc_open(&m->mpcdec, &d->audio.fmt, d->data, d->datalen)) {
		errlog(core, d->trk, NULL, "ffmpc_open()");
		ffmem_free(m);
		return NULL;
	}
	ffpcm_fmtcopy(&m->fmt, &d->audio.fmt);
	d->datalen = 0;
	d->datatype = "pcm";
	return m;
}

static void mpc_dec_close(void *ctx)
{
	mpcdec *m = ctx;
	ffmpc_close(&m->mpcdec);
	ffmem_free(m);
}

static int mpc_dec_process(void *ctx, fmed_filt *d)
{
	mpcdec *m = ctx;
	int r;
	ffstr s;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if ((d->flags & FMED_FFWD) && d->datalen != 0) {
		ffmpc_inputblock(&m->mpcdec, d->data, d->datalen, d->audio.pos);
		d->datalen = 0;
	}

	if ((int64)d->audio.seek != FMED_NULL) {
		if (d->flags & FMED_FFWD) {
			uint64 seek = ffpcm_samples(d->audio.seek, m->fmt.sample_rate);
			ffmpc_seek(&m->mpcdec, seek);
			d->audio.seek = FMED_NULL;
		} else {
			m->mpcdec.need_data = 1;
			return FMED_RMORE;
		}
	}

	r = ffmpc_decode(&m->mpcdec);

	switch (r) {
	case FFMPC_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFMPC_RDATA:
		break;

	case FFMPC_RERR:
		warnlog(core, d->trk, "mpc", "ffmpc_decode(): %s", ffmpc_errstr(&m->mpcdec));
		d->codec_err = 1;
		return FMED_RMORE;
	}

	ffmpc_audiodata(&m->mpcdec, &s);
	dbglog(core, d->trk, NULL, "decoded %L samples"
		, s.len / ffpcm_size1(&m->fmt));
	d->out = s.ptr,  d->outlen = s.len;
	d->audio.pos = ffmpc_cursample(&m->mpcdec);
	return FMED_RDATA;
}
