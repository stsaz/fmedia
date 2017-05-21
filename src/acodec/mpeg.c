/** MPEG input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/mpeg.h>
#include <FF/audio/mp3lame.h>
#include <FF/audio/pcm.h>
#include <FF/data/mmtag.h>
#include <FF/array.h>


static const fmed_core *core;
static const fmed_queue *qu;

typedef struct fmed_mpeg {
	ffmpgfile mpg;
	uint state;
	uint have_id32tag :1
		, fmt_set :1
		;
} fmed_mpeg;

typedef struct mpeg_dec {
	uint state;
	ffmpg mpg;
} mpeg_dec;

typedef struct mpeg_copy {
	ffmpgcopy mpgcpy;
} mpeg_copy;

//FMEDIA MODULE
static const void* mpeg_iface(const char *name);
static int mpeg_mod_conf(const char *name, ffpars_ctx *ctx);
static int mpeg_sig(uint signo);
static void mpeg_destroy(void);
static const fmed_mod fmed_mpeg_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&mpeg_iface, &mpeg_sig, &mpeg_destroy, &mpeg_mod_conf
};

//INPUT
static void* mpeg_open(fmed_filt *d);
static void mpeg_close(void *ctx);
static int mpeg_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mpeg_input = {
	&mpeg_open, &mpeg_process, &mpeg_close
};

static void mpeg_meta(fmed_mpeg *m, fmed_filt *d, uint type);

//DECODE
static void* mpeg_dec_open(fmed_filt *d);
static void mpeg_dec_close(void *ctx);
static int mpeg_dec_process(void *ctx, fmed_filt *d);
static const fmed_filter mpeg_decode_filt = {
	&mpeg_dec_open, &mpeg_dec_process, &mpeg_dec_close
};

//ENCODE
static void* mpeg_enc_open(fmed_filt *d);
static void mpeg_enc_close(void *ctx);
static int mpeg_enc_process(void *ctx, fmed_filt *d);
static int mpeg_enc_config(ffpars_ctx *ctx);
static const fmed_filter fmed_mpeg_enc = {
	&mpeg_enc_open, &mpeg_enc_process, &mpeg_enc_close
};

typedef struct mpeg_enc {
	uint state;
	ffmpg_enc mpg;
} mpeg_enc;

static struct mpeg_enc_conf_t {
	uint qual;
} mpeg_enc_conf;

static const ffpars_arg mpeg_enc_conf_args[] = {
	{ "quality",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_enc_conf_t, qual) },
};

//OUTPUT
static void* mpeg_out_open(fmed_filt *d);
static void mpeg_out_close(void *ctx);
static int mpeg_out_process(void *ctx, fmed_filt *d);
static int mpeg_out_config(ffpars_ctx *ctx);
static const fmed_filter fmed_mpeg_output = {
	&mpeg_out_open, &mpeg_out_process, &mpeg_out_close
};

typedef struct mpeg_out {
	uint state;
	ffmpgw mpgw;
} mpeg_out;

static int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d);

static struct mpeg_out_conf_t {
	uint min_meta_size;
} mpeg_out_conf;

static const ffpars_arg mpeg_out_conf_args[] = {
	{ "min_meta_size",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_out_conf_t, min_meta_size) },
};

//COPY
static void* mpeg_copy_open(fmed_filt *d);
static void mpeg_copy_close(void *ctx);
static int mpeg_copy_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mpeg_copy = {
	&mpeg_copy_open, &mpeg_copy_process, &mpeg_copy_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mpeg_mod;
}


static const void* mpeg_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_mpeg_input;
	else if (!ffsz_cmp(name, "decode"))
		return &mpeg_decode_filt;
	else if (!ffsz_cmp(name, "encode"))
		return &fmed_mpeg_enc;
	else if (!ffsz_cmp(name, "out"))
		return &fmed_mpeg_output;
	else if (!ffsz_cmp(name, "copy"))
		return &fmed_mpeg_copy;
	return NULL;
}

static int mpeg_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "encode"))
		return mpeg_enc_config(ctx);
	if (!ffsz_cmp(name, "out"))
		return mpeg_out_config(ctx);
	return -1;
}

static int mpeg_sig(uint signo)
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

static void mpeg_destroy(void)
{
}


static void* mpeg_open(fmed_filt *d)
{
	if (d->stream_copy) {
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpeg.copy"))
			return NULL;
		return FMED_FILT_SKIP;
	}

	fmed_mpeg *m = ffmem_tcalloc1(fmed_mpeg);
	if (m == NULL)
		return NULL;
	ffmpg_init(&m->mpg);
	m->mpg.codepage = core->getval("codepage");

	if ((int64)d->input.size != FMED_NULL) {
		ffmpg_setsize(&m->mpg.rdr, d->input.size);
		m->mpg.options = FFMPG_O_ID3V2 | FFMPG_O_APETAG | FFMPG_O_ID3V1;
	}

	return m;
}

static void mpeg_close(void *ctx)
{
	fmed_mpeg *m = ctx;
	ffmpg_fclose(&m->mpg);
	ffmem_free(m);
}

static void mpeg_meta(fmed_mpeg *m, fmed_filt *d, uint type)
{
	ffstr name, val;

	if (type == FFMPG_RID32) {
		ffstr_set(&name, m->mpg.id32tag.fr.id, 4);
		if (!m->have_id32tag) {
			m->have_id32tag = 1;
			dbglog(core, d->trk, "mpeg", "id3: ID3v2.%u.%u, size: %u"
				, (uint)m->mpg.id32tag.h.ver[0], (uint)m->mpg.id32tag.h.ver[1], ffid3_size(&m->mpg.id32tag.h));
		}

	} else if (type == FFMPG_RAPETAG) {
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
	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int mpeg_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_mpeg *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->datalen != 0) {
		ffmpg_input(&m->mpg, d->data, d->datalen);
		d->datalen = 0;
	}

again:
	switch (m->state) {
	case I_HDR:
		break;

	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL) {
			ffmpg_rseek(&m->mpg.rdr, ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpg.rdr).sample_rate));
			d->audio.seek = FMED_NULL;
		}
		break;
	}

	for (;;) {
		r = ffmpg_read(&m->mpg);

		switch (r) {
		case FFMPG_RFRAME:
			goto data;

		case FFMPG_RMORE:
			if (d->flags & FMED_FLAST) {
				if (!ffmpg_hdrok(&m->mpg)) {
					errlog(core, d->trk, NULL, "no MPEG header");
					return FMED_RERR;
				}
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMPG_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMPG_RFRAME1:
		case FFMPG_RHDR:
			dbglog(core, d->trk, NULL, "preset:%s  tool:%s  xing-frames:%u"
				, ffmpg_isvbr(&m->mpg.rdr) ? "VBR" : "CBR", m->mpg.rdr.lame.id, m->mpg.rdr.xing.frames);
			ffpcm_fmtcopy(&d->audio.fmt, &ffmpg_fmt(&m->mpg.rdr));

			d->audio.bitrate = ffmpg_bitrate(&m->mpg.rdr);
			d->audio.total = ffmpg_length(&m->mpg.rdr);
			m->state = I_DATA;

			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpeg.decode"))
				return FMED_RERR;

			if ((int64)d->audio.seek != FMED_NULL) {
				ffmpg_rseek(&m->mpg.rdr, ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpg.rdr).sample_rate));
				d->audio.seek = FMED_NULL;
				goto again;
			}
			goto data;

		case FFMPG_RID31:
		case FFMPG_RID32:
		case FFMPG_RAPETAG:
			mpeg_meta(m, d, r);
			break;

		case FFMPG_RSEEK:
			d->input.seek = ffmpg_seekoff(&m->mpg);
			return FMED_RMORE;

		case FFMPG_RWARN:
			if (m->mpg.err == FFMPG_EID32) {
				warnlog(core, d->trk, "mpeg", "ID3v2 parse (offset: %u): %s.  ID3v2.%u.%u, flags: 0x%xu, size: %u"
					, sizeof(ffid3_hdr) + ffid3_size(&m->mpg.id32tag.h) - m->mpg.id32tag.size
					, ffid3_errstr(m->mpg.id32tag.err)
					, (uint)m->mpg.id32tag.h.ver[0], (uint)m->mpg.id32tag.h.ver[1], (uint)m->mpg.id32tag.h.flags
					, ffid3_size(&m->mpg.id32tag.h));
				continue;
			}
			warnlog(core, d->trk, "mpeg", "ffmpg_read(): %s. Near sample %U, offset %U"
				, ffmpg_ferrstr(&m->mpg), ffmpg_cursample(&m->mpg.rdr), ffmpg_seekoff(&m->mpg));
			break;

		case FFMPG_RERR:
		default:
			errlog(core, d->trk, "mpeg", "ffmpg_read(): %s. Near sample %U, offset %U"
				, ffmpg_ferrstr(&m->mpg), ffmpg_cursample(&m->mpg.rdr), ffmpg_seekoff(&m->mpg));
			return FMED_RERR;
		}
	}

data:
	d->out = m->mpg.frame.ptr;
	d->outlen = m->mpg.frame.len;
	d->audio.pos = ffmpg_cursample(&m->mpg.rdr);
	return FMED_RDATA;
}


static void* mpeg_dec_open(fmed_filt *d)
{
	mpeg_dec *m;
	if (NULL == (m = ffmem_tcalloc1(mpeg_dec)))
		return NULL;

	if (0 != ffmpg_open(&m->mpg, 0)) {
		mpeg_dec_close(m);
		return NULL;
	}

	m->mpg.fmt.sample_rate = d->audio.fmt.sample_rate;
	m->mpg.fmt.channels = d->audio.fmt.channels;
	d->audio.fmt.format = m->mpg.fmt.format;
	d->audio.fmt.ileaved = m->mpg.fmt.ileaved;
	d->track->setvalstr(d->trk, "pcm_decoder", "MPEG");
	return m;
}

static void mpeg_dec_close(void *ctx)
{
	mpeg_dec *m = ctx;
	ffmpg_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_dec_process(void *ctx, fmed_filt *d)
{
	mpeg_dec *m = ctx;
	int r;

	if (d->datalen != 0) {
		ffmpg_input(&m->mpg, d->data, d->datalen);
		d->datalen = 0;
	}

	for (;;) {
	r = ffmpg_decode(&m->mpg);
	switch (r) {

	case FFMPG_RMORE:
		if (d->flags & FMED_FLAST) {
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RMORE;

	case FFMPG_RDATA:
		goto data;

	case FFMPG_RWARN:
		errlog(core, d->trk, "mpeg", "ffmpg_decode(): %s. Near sample %U"
			, ffmpg_errstr(&m->mpg), d->audio.pos);
		continue;
	}
	}

data:
	if (m->mpg.fmt.ileaved)
		d->out = (void*)m->mpg.pcmi;
	// else
	// 	d->outni = (void**)m->mpg.pcm;
	d->outlen = m->mpg.pcmlen;
	dbglog(core, d->trk, "mpeg", "output: %L PCM samples"
		, m->mpg.pcmlen / ffpcm_size1(&m->mpg.fmt));
	return FMED_RDATA;
}


static void* mpeg_copy_open(fmed_filt *d)
{
	mpeg_copy *m = ffmem_tcalloc1(mpeg_copy);
	if (m == NULL)
		return NULL;

	m->mpgcpy.options = FFMPG_WRITE_ID3V2 | FFMPG_WRITE_ID3V1 | FFMPG_WRITE_XING;
	if ((int64)d->input.size != FMED_NULL)
		ffmpg_setsize(&m->mpgcpy.rdr, d->input.size);

	d->track->setvalstr(d->trk, "data_asis", "mpeg");
	return m;
}

static void mpeg_copy_close(void *ctx)
{
	mpeg_copy *m = ctx;
	ffmem_free(m);
}

static int mpeg_copy_process(void *ctx, fmed_filt *d)
{
	mpeg_copy *m = ctx;
	int r;
	ffstr out;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->datalen != 0) {
		ffmpg_input(&m->mpgcpy, d->data, d->datalen);
		d->datalen = 0;
	}

	for (;;) {

	r = ffmpg_copy(&m->mpgcpy, &out);

	switch (r) {
	case FFMPG_RMORE:
		if (d->flags & FMED_FLAST) {
			ffmpg_copy_fin(&m->mpgcpy);
			continue;
		}
		return FMED_RMORE;

	case FFMPG_RHDR:
		ffpcm_fmtcopy(&d->audio.fmt, &ffmpg_copy_fmt(&m->mpgcpy));
		d->audio.fmt.format = FFPCM_16;
		d->audio.bitrate = ffmpg_bitrate(&m->mpgcpy.rdr);
		d->audio.total = ffmpg_length(&m->mpgcpy.rdr);
		d->track->setvalstr(d->trk, "pcm_decoder", "MPEG");

		if ((int64)d->audio.seek != FMED_NULL) {
			int64 samples = ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpgcpy.rdr).sample_rate);
			ffmpg_copy_seek(&m->mpgcpy, samples);
			d->audio.seek = FMED_NULL;
			continue;
		}
		d->meta_block = 0;
		continue;

	case FFMPG_RID32:
	case FFMPG_RID31:
		d->meta_block = 1;
		goto data;

	case FFMPG_RFRAME:
		d->audio.pos = ffmpg_cursample(&m->mpgcpy.rdr);
		if (d->audio.until != FMED_NULL && d->audio.until > 0
			&& ffpcm_time(d->audio.pos, ffmpg_copy_fmt(&m->mpgcpy).sample_rate) >= (uint64)d->audio.until) {
			dbglog(core, d->trk, NULL, "reached time %Ums", d->audio.until);
			ffmpg_copy_fin(&m->mpgcpy);
			d->audio.until = FMED_NULL;
			continue;
		}
		goto data;

	case FFMPG_RSEEK:
		d->input.seek = ffmpg_copy_seekoff(&m->mpgcpy);
		return FMED_RMORE;

	case FFMPG_ROUTSEEK:
		d->output.seek = ffmpg_copy_seekoff(&m->mpgcpy);
		continue;

	case FFMPG_RDONE:
		core->log(FMED_LOG_INFO, d->trk, NULL, "MPEG: frames:%u", ffmpg_wframes(&m->mpgcpy.writer));
		d->outlen = 0;
		return FMED_RLASTOUT;

	case FFMPG_RWARN:
		warnlog(core, d->trk, NULL, "near sample %U: ffmpg_copy(): %s"
			, ffmpg_cursample(&m->mpgcpy.rdr), ffmpg_copy_errstr(&m->mpgcpy));
		continue;

	default:
		errlog(core, d->trk, "mpeg", "ffmpg_copy() failed: %s", ffmpg_copy_errstr(&m->mpgcpy));
		return FMED_RERR;
	}
	}

data:
	d->out = out.ptr,  d->outlen = out.len;
	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, out.len);
	return FMED_RDATA;
}


static int mpeg_enc_config(ffpars_ctx *ctx)
{
	mpeg_enc_conf.qual = 2;
	ffpars_setargs(ctx, &mpeg_enc_conf, mpeg_enc_conf_args, FFCNT(mpeg_enc_conf_args));
	return 0;
}

static void* mpeg_enc_open(fmed_filt *d)
{
	mpeg_enc *m = ffmem_tcalloc1(mpeg_enc);
	if (m == NULL)
		return NULL;

	const char *copyfmt;
	if (FMED_PNULL != (copyfmt = d->track->getvalstr(d->trk, "data_asis"))) {
		if (ffsz_cmp(copyfmt, "mpeg")) {
			ffmem_free(m);
			errlog(core, d->trk, NULL, "unsupported input data format: %s", copyfmt);
			return NULL;
		}
		return FMED_FILT_SKIP;
	}

	return m;
}

static void mpeg_enc_close(void *ctx)
{
	mpeg_enc *m = ctx;
	ffmpg_enc_close(&m->mpg);
	ffmem_free(m);
}

static int mpeg_enc_process(void *ctx, fmed_filt *d)
{
	mpeg_enc *m = ctx;
	ffpcm pcm;
	int r, qual;

	switch (m->state) {
	case 0:
	case 1:
		ffpcm_fmtcopy(&pcm, &d->audio.convfmt);
		m->mpg.ileaved = d->audio.convfmt.ileaved;

		qual = (d->mpeg.quality != -1) ? d->mpeg.quality : (int)mpeg_enc_conf.qual;
		if (0 != (r = ffmpg_create(&m->mpg, &pcm, qual))) {

			if (r == FFMPG_EFMT && m->state == 0) {
				d->audio.convfmt.format = pcm.format;
				m->state = 1;
				return FMED_RMORE;
			}

			errlog(core, d->trk, "mpeg", "ffmpg_create() failed: %s", ffmpg_enc_errstr(&m->mpg));
			return FMED_RERR;
		}

		if ((int64)d->audio.total != FMED_NULL) {
			uint64 total = (d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate;
			d->output.size = ffmpg_enc_size(&m->mpg, total);
		}

		m->state = 2;
		// break

	case 2:
		m->mpg.pcm = (void*)d->data;
		m->mpg.pcmlen = d->datalen;
		m->state = 3;
		// break

	case 3:
		break;
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

		case FFMPG_RDONE:
			d->mpg_lametag = 1;
			goto data;

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
	return (r == FFMPG_RDONE) ? FMED_RDONE : FMED_RDATA;
}


static int mpeg_out_config(ffpars_ctx *ctx)
{
	mpeg_out_conf.min_meta_size = 1000;
	ffpars_setargs(ctx, &mpeg_out_conf, mpeg_out_conf_args, FFCNT(mpeg_out_conf_args));
	return 0;
}

static void* mpeg_out_open(fmed_filt *d)
{
	mpeg_out *m = ffmem_tcalloc1(mpeg_out);
	if (m == NULL)
		return NULL;
	ffmpg_winit(&m->mpgw);
	m->mpgw.options = FFMPG_WRITE_ID3V1 | FFMPG_WRITE_ID3V2;

	const char *copyfmt;
	if (FMED_PNULL != (copyfmt = d->track->getvalstr(d->trk, "data_asis"))) {
		if (ffsz_cmp(copyfmt, "mpeg")) {
			ffmem_free(m);
			errlog(core, d->trk, NULL, "unsupported input data format: %s", copyfmt);
			return NULL;
		}
		return FMED_FILT_SKIP;
	}

	return m;
}

static void mpeg_out_close(void *ctx)
{
	mpeg_out *m = ctx;
	ffmpg_wclose(&m->mpgw);
	ffmem_free(m);
}

static int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d)
{
	ssize_t r;
	fmed_trk_meta meta = {0};
	meta.flags = FMED_QUE_UNIQ;

	while (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta)) {
		if (-1 == (r = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), meta.name.ptr, meta.name.len))
			|| r == FFMMTAG_VENDOR)
			continue;

		if (0 != ffmpg_addtag(&m->mpgw, r, meta.val.ptr, meta.val.len)) {
			warnlog(core, d->trk, "mpeg", "%s", "can't add tag: %S", &meta.name);
		}
	}
	return 0;
}

static int mpeg_out_process(void *ctx, fmed_filt *d)
{
	mpeg_out *m = ctx;
	int r;
	ffstr s;

	switch (m->state) {
	case 0:
		m->mpgw.min_meta = mpeg_out_conf.min_meta_size;
		if (0 != mpeg_out_addmeta(m, d))
			return FMED_RERR;
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)"mpeg.encode"))
			return FMED_RERR;
		m->state = 2;
		return FMED_RMORE;

	case 2:
		break;
	}

	if (d->mpg_lametag) {
		m->mpgw.lametag = 1;
		m->mpgw.fin = 1;
	}

	for (;;) {
		r = ffmpg_writeframe(&m->mpgw, (void*)d->data, d->datalen, &s);
		switch (r) {

		case FFMPG_RID32:
		case FFMPG_RID31:
			goto data;

		case FFMPG_RDATA:
			d->datalen = 0;
			goto data;

		case FFMPG_RMORE:
			if (!(d->flags & FMED_FLAST)) {
				m->state = 2;
				return FMED_RMORE;
			}
			m->mpgw.fin = 1;
			break;

		case FFMPG_RSEEK:
			d->output.seek = ffmpg_wseekoff(&m->mpgw);
			break;

		case FFMPG_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		default:
			errlog(core, d->trk, "mpeg", "ffmpg_writeframe() failed: %s", ffmpg_werrstr(&m->mpgw));
			return FMED_RERR;
		}
	}

data:
	d->out = s.ptr,  d->outlen = s.len;
	dbglog(core, d->trk, "mpeg", "output: %L bytes"
		, s.len);
	return FMED_RDATA;
}
