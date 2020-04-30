/** MPEG Layer3 (.mp3) reader/writer.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/mp3.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>
#include <FF/array.h>


extern const fmed_core *core;
extern const fmed_queue *qu;

//INPUT
static void* mpeg_open(fmed_filt *d);
static void mpeg_close(void *ctx);
static int mpeg_process(void *ctx, fmed_filt *d);
const fmed_filter fmed_mpeg_input = {
	&mpeg_open, &mpeg_process, &mpeg_close
};

typedef struct mpeg_in {
	ffmpgfile mpg;
	uint state;
	uint have_id32tag :1
		, seeking :1
		;
} mpeg_in;

static void mpeg_meta(mpeg_in *m, fmed_filt *d, uint type);

//OUTPUT
static void* mpeg_out_open(fmed_filt *d);
static void mpeg_out_close(void *ctx);
static int mpeg_out_process(void *ctx, fmed_filt *d);
extern int mpeg_out_config(ffpars_ctx *ctx);
const fmed_filter fmed_mpeg_output = {
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
const fmed_filter fmed_mpeg_copy = {
	&mpeg_copy_open, &mpeg_copy_process, &mpeg_copy_close
};

typedef struct mpeg_copy {
	ffmpgcopy mpgcpy;
	uint64 until;
} mpeg_copy;


static void* mpeg_open(fmed_filt *d)
{
	if (d->stream_copy && !d->track->cmd(d->trk, FMED_TRACK_META_HAVEUSER)) {

		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpeg.copy"))
			return NULL;
		return FMED_FILT_SKIP;
	}

	mpeg_in *m = ffmem_new(mpeg_in);
	if (m == NULL)
		return NULL;
	ffmpg_fopen(&m->mpg);
	m->mpg.codepage = core->getval("codepage");

	if ((int64)d->input.size != FMED_NULL) {
		ffmpg_setsize(&m->mpg.rdr, d->input.size);
		m->mpg.options = FFMPG_O_ID3V2 | FFMPG_O_APETAG | FFMPG_O_ID3V1;
	}

	return m;
}

static void mpeg_close(void *ctx)
{
	mpeg_in *m = ctx;
	ffmpg_fclose(&m->mpg);
	ffmem_free(m);
}

static void mpeg_meta(mpeg_in *m, fmed_filt *d, uint type)
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
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static int mpeg_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	mpeg_in *m = ctx;
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
		if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
			m->seeking = 1;
			ffmpg_rseek(&m->mpg.rdr, ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpg.rdr).sample_rate));
			if (d->stream_copy)
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

		case FFMPG_RXING:
			continue;

		case FFMPG_RHDR:
			dbglog(core, d->trk, NULL, "preset:%s  tool:%s  xing-frames:%u"
				, ffmpg_isvbr(&m->mpg.rdr) ? "VBR" : "CBR", m->mpg.rdr.lame.id, m->mpg.rdr.xing.frames);
			ffpcm_fmtcopy(&d->audio.fmt, &ffmpg_fmt(&m->mpg.rdr));
			d->audio.fmt.format = FFPCM_16;
			d->audio.decoder = "MPEG";
			d->datatype = "mpeg";

			d->audio.bitrate = ffmpg_bitrate(&m->mpg.rdr);
			d->audio.total = ffmpg_length(&m->mpg.rdr);

			if (d->input_info)
				return FMED_RDONE;

			if (m->mpg.rdr.duration_inaccurate) {
				dbglog(core, d->trk, NULL, "duration may be inaccurate");
			}
			/* Broken .mp3 files prevent from writing header-first .mp4 files.
			This tells .mp4 writer not to trust us. */
			d->duration_inaccurate = 1;
			m->state = I_DATA;
			fmed_setval("mpeg_delay", m->mpg.rdr.delay);

			if (!d->stream_copy
				&& 0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpeg.decode"))
				return FMED_RERR;

			if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
				m->seeking = 1;
				ffmpg_rseek(&m->mpg.rdr, ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpg.rdr).sample_rate));
			}

			goto again;

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
	if (m->seeking)
		m->seeking = 0;
	d->out = m->mpg.frame.ptr;
	d->outlen = m->mpg.frame.len;
	d->audio.pos = ffmpg_cursample(&m->mpg.rdr);
	dbglog(core, d->trk, NULL, "passing frame #%u  samples:%u[%U]  size:%u  br:%u  off:%xU"
		, m->mpg.rdr.frno, (uint)m->mpg.rdr.frsamps, d->audio.pos, (uint)m->mpg.frame.len
		, ffmpg_hdr_bitrate((void*)m->mpg.frame.ptr), m->mpg.rdr.off - m->mpg.frame.len);
	return FMED_RDATA;
}


static void* mpeg_copy_open(fmed_filt *d)
{
	mpeg_copy *m = ffmem_new(mpeg_copy);
	if (m == NULL)
		return NULL;

	m->mpgcpy.options = FFMPG_WRITE_ID3V2 | FFMPG_WRITE_ID3V1 | FFMPG_WRITE_XING;
	if ((int64)d->input.size != FMED_NULL)
		ffmpg_setsize(&m->mpgcpy.rdr, d->input.size);

	m->until = (uint64)-1;
	if (d->audio.until != FMED_NULL) {
		m->until = d->audio.until;
		d->audio.until = FMED_NULL;
	}

	d->datatype = "mpeg";
	return m;
}

static void mpeg_copy_close(void *ctx)
{
	mpeg_copy *m = ctx;
	ffmpg_copy_close(&m->mpgcpy);
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

	case FFMPG_RXING:
		continue;

	case FFMPG_RHDR:
		ffpcm_fmtcopy(&d->audio.fmt, &ffmpg_copy_fmt(&m->mpgcpy));
		d->audio.fmt.format = FFPCM_16;
		d->audio.bitrate = ffmpg_bitrate(&m->mpgcpy.rdr);
		d->audio.total = ffmpg_length(&m->mpgcpy.rdr);
		d->audio.decoder = "MPEG";
		d->meta_block = 0;

		if ((int64)d->audio.seek != FMED_NULL) {
			int64 samples = ffpcm_samples(d->audio.seek, ffmpg_fmt(&m->mpgcpy.rdr).sample_rate);
			ffmpg_copy_seek(&m->mpgcpy, samples);
			d->audio.seek = FMED_NULL;
			continue;
		}
		continue;

	case FFMPG_RID32:
	case FFMPG_RID31:
		d->meta_block = 1;
		goto data;

	case FFMPG_RFRAME:
		d->audio.pos = ffmpg_cursample(&m->mpgcpy.rdr);
		if (ffpcm_time(d->audio.pos, ffmpg_copy_fmt(&m->mpgcpy).sample_rate) >= m->until) {
			dbglog(core, d->trk, NULL, "reached time %Ums", d->audio.until);
			ffmpg_copy_fin(&m->mpgcpy);
			m->until = (uint64)-1;
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


int mpeg_out_config(ffpars_ctx *ctx)
{
	mpeg_out_conf.min_meta_size = 1000;
	ffpars_setargs(ctx, &mpeg_out_conf, mpeg_out_conf_args, FFCNT(mpeg_out_conf_args));
	return 0;
}

static int mpeg_have_trkmeta(fmed_filt *d)
{
	fmed_trk_meta meta;
	ffmem_tzero(&meta);
	if (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta))
		return 1;
	return 0;
}

static void* mpeg_out_open(fmed_filt *d)
{
	if (ffsz_eq(d->datatype, "mpeg") && !mpeg_have_trkmeta(d))
		return FMED_FILT_SKIP; // mpeg.copy is used in this case

	mpeg_out *m = ffmem_new(mpeg_out);
	if (m == NULL)
		return NULL;
	ffmpg_winit(&m->mpgw);
	m->mpgw.options = FFMPG_WRITE_ID3V1 | FFMPG_WRITE_ID3V2;
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
	fmed_trk_meta meta;
	ffmem_tzero(&meta);
	meta.flags = FMED_QUE_UNIQ;

	while (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta)) {
		if (-1 == (r = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), meta.name.ptr, meta.name.len))
			|| r == FFMMTAG_VENDOR)
			continue;

		if (0 != ffmpg_addtag(&m->mpgw, r, meta.val.ptr, meta.val.len)) {
			warnlog(core, d->trk, "mpeg", "can't add tag: %S", &meta.name);
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
		m->state = 2;
		if (ffsz_eq(d->datatype, "mpeg"))
			break;
		else if (!ffsz_eq(d->datatype, "pcm")) {
			errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
			return FMED_RERR;
		}

		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, (void*)"mpeg.encode"))
			return FMED_RERR;
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
