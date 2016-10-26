/** MP4 (AAC, ALAC) input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/data/mp4.h>
#include <FF/data/mmtag.h>
#include <FF/audio/aac.h>


static const fmed_core *core;
static const fmed_queue *qu;

static struct aac_out_conf_t {
	uint aot;
	uint qual;
	uint afterburner;
	uint bandwidth;
} aac_out_conf;


typedef struct mp4 {
	ffmp4 mp;
	uint state;
} mp4;

typedef struct mp4_out {
	uint state;
	ffpcm fmt;
	ffmp4_cook mp;
	ffaac_enc aac;
} mp4_out;


//FMEDIA MODULE
static const void* mp4_iface(const char *name);
static int mp4_sig(uint signo);
static void mp4_destroy(void);
static const fmed_mod fmed_mp4_mod = {
	&mp4_iface, &mp4_sig, &mp4_destroy
};

//INPUT
static void* mp4_in_create(fmed_filt *d);
static void mp4_in_free(void *ctx);
static int mp4_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mp4_input = {
	&mp4_in_create, &mp4_in_decode, &mp4_in_free
};

//AAC ENCODE
static int aac_out_config(ffpars_ctx *ctx);
static void* mp4_out_create(fmed_filt *d);
static void mp4_out_free(void *ctx);
static int mp4_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter mp4aac_output = {
	&mp4_out_create, &mp4_out_encode, &mp4_out_free, &aac_out_config
};

static void mp4_meta(mp4 *m, fmed_filt *d);
static int mp4_out_addmeta(mp4_out *m, fmed_filt *d);


static const ffpars_arg aac_out_conf_args[] = {
	{ "profile",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, aot) },
	{ "quality",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, qual) },
	{ "afterburner",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, afterburner) },
	{ "bandwidth",	FFPARS_TINT,  FFPARS_DSTOFF(struct aac_out_conf_t, bandwidth) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mp4_mod;
}


static const void* mp4_iface(const char *name)
{
	if (!ffsz_cmp(name, "input"))
		return &fmed_mp4_input;
	else if (!ffsz_cmp(name, "aac-encode"))
		return &mp4aac_output;
	return NULL;
}

static int mp4_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void mp4_destroy(void)
{
}


static void* mp4_in_create(fmed_filt *d)
{
	mp4 *m = ffmem_tcalloc1(mp4);
	if (m == NULL)
		return NULL;

	ffmp4_init(&m->mp);

	if ((int64)d->input.size != FMED_NULL)
		m->mp.total_size = d->input.size;

	return m;
}

static void mp4_in_free(void *ctx)
{
	mp4 *m = ctx;
	ffmp4_close(&m->mp);
	ffmem_free(m);
}

static void mp4_meta(mp4 *m, fmed_filt *d)
{
	ffstr name, val;
	if (m->mp.tag == 0)
		return;
	ffstr_setz(&name, ffmmtag_str[m->mp.tag]);
	val = m->mp.tagval;

	dbglog(core, d->trk, "mp4", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int mp4_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA, };
	mp4 *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	m->mp.data = d->data;
	m->mp.datalen = d->datalen;

	for (;;) {

	switch (m->state) {

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			uint64 seek = ffpcm_samples(d->audio.seek, m->mp.fmt.sample_rate);
			ffmp4_seek(&m->mp, seek);
		}

	case I_HDR:
		r = ffmp4_read(&m->mp);
		switch (r) {
		case FFMP4_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "mp4", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMP4_RHDR: {
			d->track->setvalstr(d->trk, "pcm_decoder", ffmp4_codec(m->mp.codec));
			ffpcm_fmtcopy(&d->audio.fmt, &m->mp.fmt);

			d->audio.total = ffmp4_totalsamples(&m->mp);

			const char *filt;
			if (m->mp.codec == FFMP4_ALAC) {
				filt = "alac.decode";
				d->audio.bitrate = ffmp4_bitrate(&m->mp);
			}
			else if (m->mp.codec == FFMP4_AAC) {
				filt = "aac.decode";
				fmed_setval("audio_enc_delay", m->mp.enc_delay);
				fmed_setval("audio_end_padding", m->mp.end_padding);
				d->audio.bitrate = (m->mp.aac_brate != 0) ? m->mp.aac_brate : ffmp4_bitrate(&m->mp);
			}
			else {
				errlog(core, d->trk, "mp4", "%s: decoding unsupported", ffmp4_codec(m->mp.codec));
				return FMED_RERR;
			}
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)filt))
				return FMED_RERR;
			d->data = m->mp.data,  d->datalen = m->mp.datalen;
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			return FMED_RDATA;
		}

		case FFMP4_RTAG:
			mp4_meta(m, d);
			break;

		case FFMP4_RMETAFIN:
			if (d->input_info)
				return FMED_ROK;

			m->state = I_DATA;
			continue;

		case FFMP4_RDATA:
			d->audio.pos = ffmp4_cursample(&m->mp);
			d->data = m->mp.data,  d->datalen = m->mp.datalen;
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			return FMED_RDATA;

		case FFMP4_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMP4_RSEEK:
			d->input.seek = m->mp.off;
			return FMED_RMORE;

		case FFMP4_RWARN:
			warnlog(core, d->trk, "mp4", "ffmp4_read(): at offset 0x%xU: %s"
				, m->mp.off, ffmp4_errstr(&m->mp));
			break;

		case FFMP4_RERR:
			errlog(core, d->trk, "mp4", "ffmp4_read(): %s", ffmp4_errstr(&m->mp));
			return FMED_RERR;
		}
		break;
	}

	}

	//unreachable
}


static int aac_out_config(ffpars_ctx *ctx)
{
	aac_out_conf.aot = AAC_LC;
	aac_out_conf.qual = 256;
	aac_out_conf.afterburner = 1;
	aac_out_conf.bandwidth = 0;
	ffpars_setargs(ctx, &aac_out_conf, aac_out_conf_args, FFCNT(aac_out_conf_args));
	return 0;
}

static void* mp4_out_create(fmed_filt *d)
{
	mp4_out *m = ffmem_tcalloc1(mp4_out);
	if (m == NULL)
		return NULL;
	return m;
}

static void mp4_out_free(void *ctx)
{
	mp4_out *m = ctx;
	ffaac_enc_close(&m->aac);
	ffmp4_wclose(&m->mp);
	ffmem_free(m);
}

static int mp4_out_addmeta(mp4_out *m, fmed_filt *d)
{
	uint i;
	ffstr name, *val;
	void *qent;

	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| ffstr_eqcz(&name, "vendor"))
			continue;

		int tag;
		if (-1 == (tag = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), name.ptr, name.len))) {
			warnlog(core, d->trk, "mp4", "unsupported tag: %S", &name);
			continue;
		}

		if (0 != ffmp4_addtag(&m->mp, tag, val->ptr, val->len)) {
			warnlog(core, d->trk, "mp4", "can't add tag: %S", &name);
		}
	}
	return 0;
}

static int mp4_out_encode(void *ctx, fmed_filt *d)
{
	mp4_out *m = ctx;
	int r;

	enum { I_CONV, I_INIT, I_ENC, I_MP4 };

	for (;;) {
	switch (m->state) {

	case I_CONV:
		fmed_setval("conv_pcm_format", FFPCM_16);
		fmed_setval("conv_pcm_ileaved", 1);
		m->state = I_INIT;
		return FMED_RMORE;

	case I_INIT: {
		ffpcm_fmtcopy(&m->fmt, &d->audio.fmt);

		if (m->fmt.format != FFPCM_16LE || !d->audio.fmt.ileaved) {
			errlog(core, d->trk, NULL, "unsupported input PCM format");
			return FMED_RERR;
		}

		if ((int64)d->audio.total == FMED_NULL) {
			errlog(core, d->trk, NULL, "total_samples unknown");
			return FMED_RERR;
		}

		int qual;
		if (FMED_NULL == (qual = fmed_getval("aac-quality")))
			qual = aac_out_conf.qual;
		if (qual > 5 && qual < 8000)
			qual *= 1000;

		m->aac.info.aot = aac_out_conf.aot;
		m->aac.info.afterburner = aac_out_conf.afterburner;
		m->aac.info.bandwidth = aac_out_conf.bandwidth;

		if (0 != (r = ffaac_create(&m->aac, &m->fmt, qual))) {
			errlog(core, d->trk, NULL, "ffaac_create(): %s", ffaac_enc_errstr(&m->aac));
			return FMED_RERR;
		}

		ffstr asc = ffaac_enc_conf(&m->aac);
		m->mp.info.total_samples = d->audio.total - d->audio.pos;
		m->mp.info.frame_samples = ffaac_enc_frame_samples(&m->aac);
		m->mp.info.enc_delay = m->aac.info.enc_delay;
		m->mp.info.bitrate = ffaac_bitrate(&m->aac, qual);
		if (0 != (r = ffmp4_create_aac(&m->mp, &m->fmt, &asc))) {
			errlog(core, d->trk, NULL, "ffmp4_create_aac(): %s", ffmp4_werrstr(&m->mp));
			return FMED_RERR;
		}

		if (0 != mp4_out_addmeta(m, d))
			return FMED_RERR;

		d->output.size = ffmp4_wsize(&m->mp);

		m->state = I_ENC;
		// break
	}

	case I_ENC:
		if (d->flags & FMED_FLAST)
			m->aac.fin = 1;
		m->aac.pcm = (void*)d->data,  m->aac.pcmlen = d->datalen;
		r = ffaac_encode(&m->aac);
		switch (r) {
		case FFAAC_RDONE:
			m->mp.fin = 1;
			m->state = I_MP4;
			continue;

		case FFAAC_RMORE:
			return FMED_RMORE;

		case FFAAC_RDATA:
			break;

		case FFAAC_RERR:
			errlog(core, d->trk, NULL, "ffaac_encode(): %s", ffaac_enc_errstr(&m->aac));
			return FMED_RERR;
		}
		dbglog(core, d->trk, NULL, "encoded %L samples into %L bytes"
			, (d->datalen - m->aac.pcmlen) / ffpcm_size1(&m->fmt), m->aac.datalen);
		d->data = (void*)m->aac.pcm,  d->datalen = m->aac.pcmlen;
		m->mp.data = m->aac.data,  m->mp.datalen = m->aac.datalen;
		m->state = I_MP4;
		// break

	case I_MP4:
		r = ffmp4_write(&m->mp);
		switch (r) {
		case FFMP4_RMORE:
			m->state = I_ENC;
			continue;

		case FFMP4_RSEEK:
			d->output.seek = m->mp.off;
			continue;

		case FFMP4_RDATA:
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			return FMED_RDATA;

		case FFMP4_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFMP4_RWARN:
			warnlog(core, d->trk, NULL, "ffmp4_write(): %s", ffmp4_werrstr(&m->mp));
			continue;

		case FFMP4_RERR:
			errlog(core, d->trk, NULL, "ffmp4_write(): %s", ffmp4_werrstr(&m->mp));
			return FMED_RERR;
		}
	}
	}
}
