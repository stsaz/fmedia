/** MPEG input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/mpeg.h>
#include <FF/audio/mp3lame.h>
#include <FF/audio/pcm.h>
#include <FF/data/mmtag.h>
#include <FF/data/utf8.h>
#include <FF/array.h>
#include <FF/number.h>


static const fmed_core *core;
static const fmed_queue *qu;

typedef struct fmed_mpeg {
	ffmpg mpg;
	uint state;
	uint have_id32tag :1
		, fmt_set :1
		;
} fmed_mpeg;

typedef struct mpeg_out {
	uint state;
	ffmpg_enc mpg;
} mpeg_out;

static struct mpeg_conf_t {
	uint meta_codepage;
} mpeg_conf;

static struct mpeg_out_conf_t {
	uint qual;
	uint min_meta_size;
} mpeg_out_conf;

//FMEDIA MODULE
static const void* mpeg_iface(const char *name);
static int mpeg_sig(uint signo);
static void mpeg_destroy(void);
static const fmed_mod fmed_mpeg_mod = {
	&mpeg_iface, &mpeg_sig, &mpeg_destroy
};

//DECODE
static void* mpeg_open(fmed_filt *d);
static void mpeg_close(void *ctx);
static int mpeg_process(void *ctx, fmed_filt *d);
static int mpeg_config(ffpars_ctx *ctx);
static const fmed_filter fmed_mpeg_input = {
	&mpeg_open, &mpeg_process, &mpeg_close, &mpeg_config
};

static void mpeg_meta(fmed_mpeg *m, fmed_filt *d);
static int mpeg_copy_prep(fmed_mpeg *m, fmed_filt *d);
static int mpeg_copy_read(fmed_mpeg *m, fmed_filt *d);

//ENCODE
static void* mpeg_out_open(fmed_filt *d);
static void mpeg_out_close(void *ctx);
static int mpeg_out_process(void *ctx, fmed_filt *d);
static int mpeg_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_mpeg_output = {
	&mpeg_out_open, &mpeg_out_process, &mpeg_out_close, &mpeg_out_config
};

static int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d);
static int mpeg_out_copy(mpeg_out *m, fmed_filt *d);

static const ffpars_arg mpeg_out_conf_args[] = {
	{ "quality",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_out_conf_t, qual) },
	{ "min_meta_size",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_out_conf_t, min_meta_size) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_mpeg_mod;
}


static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "decode"))
		return &fmed_mpeg_input;
	else if (!ffsz_cmp(name, "encode"))
		return &fmed_mpeg_output;
	return NULL;
}

static int mpeg_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void mpeg_destroy(void)
{
}


static int mpeg_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &mpeg_conf, NULL, 0);
	return 0;
}

static void* mpeg_open(fmed_filt *d)
{
	int64 total_size;
	fmed_mpeg *m = ffmem_tcalloc1(fmed_mpeg);
	if (m == NULL)
		return NULL;
	ffmpg_init(&m->mpg);
	if (FMED_NULL != fmed_getval("total_size"))
		m->mpg.options = FFMPG_O_ID3V2 | FFMPG_O_APETAG | FFMPG_O_ID3V1;
	m->mpg.codepage = core->getval("codepage");

	if (FMED_NULL != (total_size = fmed_getval("total_size")))
		m->mpg.total_size = total_size;
	return m;
}

static void mpeg_close(void *ctx)
{
	fmed_mpeg *m = ctx;
	ffmpg_close(&m->mpg);
	ffmem_free(m);
}

static void mpeg_meta(fmed_mpeg *m, fmed_filt *d)
{
	ffstr name, val;

	if (m->mpg.is_id32tag) {
		ffstr_set(&name, m->mpg.id32tag.fr.id, 4);
		if (!m->have_id32tag) {
			m->have_id32tag = 1;
			dbglog(core, d->trk, "mpeg", "id3: ID3v2.%u.%u, size: %u"
				, (uint)m->mpg.id32tag.h.ver[0], (uint)m->mpg.id32tag.h.ver[1], ffid3_size(&m->mpg.id32tag.h));
		}

	} else if (m->mpg.is_apetag) {
		name = m->mpg.apetag.name;
		if (FFAPETAG_FBINARY == (m->mpg.apetag.flags & FFAPETAG_FMASK)) {
			dbglog(core, d->trk, "mpeg", "skipping binary tag: %S", &m->mpg.apetag.name);
			return;
		}
	}

	if (m->mpg.tag != 0)
		ffstr_setz(&name, ffmmtag_str[m->mpg.tag]);

	ffstr_set2(&val, &m->mpg.tagval);

	dbglog(core, d->trk, "mpeg", "tag: %S: %S", &name, &val);

	if (m->mpg.tag != 0)
		qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int mpeg_copy_prep(fmed_mpeg *m, fmed_filt *d)
{
	if (1 != fmed_getval("stream_copy"))
		return 0;

	d->track->setvalstr(d->trk, "data_asis", "mpeg");
	return 1;
}

static int mpeg_copy_read(fmed_mpeg *m, fmed_filt *d)
{
	int r;

	for (;;) {
	r = ffmpg_readframe(&m->mpg);

	switch (r) {
	case FFMPG_RFRAME:
		goto data;

	case FFMPG_RDONE:
		d->outlen = 0;
		return FMED_RLASTOUT;

	case FFMPG_RMORE:
		if (d->flags & FMED_FLAST) {
			dbglog(core, d->trk, NULL, "file is incomplete");
			d->outlen = 0;
			return FMED_RLASTOUT;
		}
		return FMED_RMORE;

	case FFMPG_RSEEK:
		fmed_setval("input_seek", m->mpg.off);
		return FMED_RMORE;

	case FFMPG_RWARN:
		warnlog(core, d->trk, NULL, "near sample %U: ffmpg_readframe(): %s"
			, ffmpg_cursample(&m->mpg), ffmpg_errstr(&m->mpg));
		break;

	default:
		errlog(core, d->trk, NULL, "ffmpg_readframe(): %s", ffmpg_errstr(&m->mpg));
		return FMED_RERR;
	}
	}

data:
	if (!m->fmt_set) {
		m->fmt_set = 1;
		m->mpg.fmt.format = FFPCM_16;
		fmed_setpcm(d, (void*)&m->mpg.fmt);
		fmed_setval("bitrate", ffmpg_bitrate(&m->mpg));
		fmed_setval("total_samples", m->mpg.total_samples);

		int64 seek_time, samples;
		if (FMED_NULL != (seek_time = fmed_popval("seek_time"))) {
			samples = ffpcm_samples(seek_time, m->mpg.fmt.sample_rate);
			ffmpg_seek(&m->mpg, samples);
		}
	}

	d->track->setval(d->trk, "current_position", ffmpg_cursample(&m->mpg));
	dbglog(core, d->trk, NULL, "passed %L bytes", m->mpg.datalen);
	d->data = m->mpg.data,  d->datalen = m->mpg.datalen;
	ffstr s = ffmpg_framedata(&m->mpg);
	d->out = s.ptr,  d->outlen = s.len;
	return FMED_RDATA;
}

static int mpeg_process(void *ctx, fmed_filt *d)
{
	enum { I_CHK_ASIS, I_HDR, I_DATA, I_ASIS };
	fmed_mpeg *m = ctx;
	int r;
	int64 seek_time;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	m->mpg.data = d->data;
	m->mpg.datalen = d->datalen;

again:
	switch (m->state) {
	case I_CHK_ASIS:
		if (mpeg_copy_prep(m, d))
			m->state = I_ASIS;
		else {
			m->state = I_HDR;
			goto again;
		}
		// break

	case I_ASIS:
		return mpeg_copy_read(m, d);

	case I_HDR:
		break;

	case I_DATA:
		if (FMED_NULL != (seek_time = fmed_popval("seek_time")))
			ffmpg_seek(&m->mpg, ffpcm_samples(seek_time, m->mpg.fmt.sample_rate));
		break;
	}

	for (;;) {
		r = ffmpg_decode(&m->mpg);

		switch (r) {
		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (d->flags & FMED_FLAST) {
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMPG_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMPG_RHDR:
			dbglog(core, d->trk, NULL, "preset:%s  tool:%s  xing-frames:%u"
				, ffmpg_isvbr(&m->mpg) ? "VBR" : "CBR", m->mpg.lame.id, m->mpg.xing.frames);
			fmed_setpcm(d, (void*)&m->mpg.fmt);
			d->track->setvalstr(d->trk, "pcm_decoder", "MPEG");
			fmed_setval("pcm_ileaved", m->mpg.fmt.ileaved);
			fmed_setval("bitrate", ffmpg_bitrate(&m->mpg));
			fmed_setval("total_samples", m->mpg.total_samples);
			m->state = I_DATA;
			goto again;

		case FFMPG_RTAG:
			mpeg_meta(m, d);
			break;

		case FFMPG_RSEEK:
			fmed_setval("input_seek", ffmpg_seekoff(&m->mpg));
			return FMED_RMORE;

		case FFMPG_RWARN:
			if (m->mpg.err == FFMPG_ETAG) {
				warnlog(core, d->trk, "mpeg", "id3: parse (offset: %u): ID3v2.%u.%u, flags: %u, size: %u"
					, sizeof(ffid3_hdr) + ffid3_size(&m->mpg.id32tag.h) - m->mpg.id32tag.size
					, (uint)m->mpg.id32tag.h.ver[0], (uint)m->mpg.id32tag.h.ver[1], (uint)m->mpg.id32tag.h.flags
					, ffid3_size(&m->mpg.id32tag.h));
				continue;
			}
			warnlog(core, d->trk, "mpeg", "ffmpg_decode(): %s. Near sample %U, offset %U"
				, ffmpg_errstr(&m->mpg), ffmpg_cursample(&m->mpg), m->mpg.off);
			break;

		case FFMPG_RERR:
		default:
			errlog(core, d->trk, "mpeg", "ffmpg_decode(): %s. Near sample %U, offset %U"
				, ffmpg_errstr(&m->mpg), ffmpg_cursample(&m->mpg), m->mpg.off);
			return FMED_RERR;
		}
	}

data:
	d->data = m->mpg.data;
	d->datalen = m->mpg.datalen;
	if (m->mpg.fmt.ileaved)
		d->out = (void*)m->mpg.pcmi;
	else
		d->outni = (void**)m->mpg.pcm;
	d->outlen = m->mpg.pcmlen;
	fmed_setval("current_position", ffmpg_cursample(&m->mpg));

	dbglog(core, d->trk, "mpeg", "output: %L PCM samples"
		, d->outlen / ffpcm_size1(&m->mpg.fmt));
	return FMED_RDATA;
}


static int mpeg_out_config(ffpars_ctx *ctx)
{
	mpeg_out_conf.qual = 2;
	mpeg_out_conf.min_meta_size = 1000;
	ffpars_setargs(ctx, &mpeg_out_conf, mpeg_out_conf_args, FFCNT(mpeg_out_conf_args));
	return 0;
}

enum { W_FR_DATA = 4 };

static void* mpeg_out_open(fmed_filt *d)
{
	mpeg_out *m = ffmem_tcalloc1(mpeg_out);
	if (m == NULL)
		return NULL;
	m->mpg.options = FFMPG_WRITE_ID3V1 | FFMPG_WRITE_ID3V2;

	const char *copyfmt;
	if (FMED_PNULL != (copyfmt = d->track->getvalstr(d->trk, "data_asis"))) {
		if (ffsz_cmp(copyfmt, "mpeg")) {
			ffmem_free(m);
			errlog(core, d->trk, NULL, "unsupported input data format: %s", copyfmt);
			return NULL;
		}
		ffmpg_create_copy(&m->mpg);
		m->state = W_FR_DATA;
	}

	return m;
}

static void mpeg_out_close(void *ctx)
{
	mpeg_out *m = ctx;
	ffmpg_enc_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d)
{
	uint i;
	ffstr name, *val;
	void *qent;
	ssize_t r;

	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| -1 == (r = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), name.ptr, name.len)))
			continue;

		if (0 != ffmpg_addtag(&m->mpg, r, val->ptr, val->len)) {
			syserrlog(core, d->trk, "mpeg", "%s", "add meta tag");
			return -1;
		}
	}
	return 0;
}

static int mpeg_out_copy(mpeg_out *m, fmed_filt *d)
{
	ffstr frame;
	if (d->datalen == 0 && !(d->flags & FMED_FLAST))
		return FMED_RMORE;
	if (d->flags & FMED_FLAST)
		m->mpg.fin = 1;

	for (;;) {
		int r = ffmpg_writeframe(&m->mpg, (void*)d->data, d->datalen, &frame);
		switch (r) {

		case FFMPG_RDATA:
			goto data;

		case FFMPG_RSEEK:
			fmed_setval("output_seek", ffmpg_enc_seekoff(&m->mpg));
			break;

		case FFMPG_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		default:
			errlog(core, d->trk, "mpeg", "ffmpg_writeframe() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	d->out = frame.ptr,  d->outlen = frame.len;
	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, m->mpg.datalen);
	d->datalen = 0;
	return FMED_RDATA;
}

static int mpeg_out_process(void *ctx, fmed_filt *d)
{
	mpeg_out *m = ctx;
	ffpcm pcm;
	int r, qual;

	switch (m->state) {
	case 0:
	case 1:
		pcm.format = (int)fmed_getval("pcm_format");
		pcm.sample_rate = (int)fmed_getval("pcm_sample_rate");
		pcm.channels = (int)fmed_getval("pcm_channels");

		if (FMED_NULL == (qual = (int)fmed_getval("mpeg-quality")))
			qual = mpeg_out_conf.qual;

		m->mpg.ileaved = fmed_getval("pcm_ileaved");
		if (0 != (r = ffmpg_create(&m->mpg, &pcm, qual))) {

			if (r == FFMPG_EFMT && m->state == 0) {
				fmed_setval("conv_pcm_format", pcm.format);
				m->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, "mpeg", "ffmpg_create() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}
		m->mpg.min_meta = mpeg_out_conf.min_meta_size;

		if (0 != mpeg_out_addmeta(m, d))
			return FMED_RERR;

		m->state = 2;
		// break

	case 2:
		m->mpg.pcm = (void*)d->data;
		m->mpg.pcmlen = d->datalen;
		m->state = 3;
		// break

	case 3:
		break;

	default:
		return mpeg_out_copy(m, d);
	}

	for (;;) {
		r = ffmpg_encode(&m->mpg);
		switch (r) {

		case FFMPG_RDATA:
			goto data;

		case FFMPG_RMORE:
			if (!(d->flags & FMED_FLAST)) {
				m->state = 2;
				return FMED_RMORE;
			}
			m->mpg.fin = 1;
			break;

		case FFMPG_RSEEK:
			fmed_setval("output_seek", ffmpg_enc_seekoff(&m->mpg));
			break;

		case FFMPG_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		default:
			errlog(core, d->trk, "mpeg", "ffmpg_encode() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	d->out = m->mpg.data;
	d->outlen = m->mpg.datalen;

	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, m->mpg.datalen);
	return FMED_RDATA;
}
