/** MPEG Layer3 (.mp3) reader/writer.
Copyright (c) 2017 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/mp3.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>
#include <FF/array.h>


extern const fmed_core *core;
extern const fmed_queue *qu;

#include <format/mp3-write.h>
#include <format/mp3-copy.h>

typedef struct mpeg_in {
	ffmpgfile mpg;
	ffstr in;
	uint state;
	uint have_id32tag :1
		, seeking :1
		;
} mpeg_in;

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
		m->in = d->data_in;
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

	ffstr out;
	for (;;) {
		r = ffmpg_read(&m->mpg, &m->in, &out);

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

			if (d->audio.abs_seek != 0) {
				d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "plist.cuehook");
				m->seeking = 1;
				uint64 samples = fmed_apos_samples(d->audio.abs_seek, d->audio.fmt.sample_rate);
				ffmpg_rseek(&m->mpg.rdr, samples);
			}

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
	d->audio.pos = ffmpg_cursample(&m->mpg.rdr);
	dbglog(core, d->trk, NULL, "passing frame #%u  samples:%u[%U]  size:%u  br:%u  off:%xU"
		, m->mpg.rdr.frno, (uint)m->mpg.rdr.frsamps, d->audio.pos, (uint)out.len
		, ffmpg_hdr_bitrate((void*)out.ptr), m->mpg.rdr.off - out.len);
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter fmed_mpeg_input = {
	mpeg_open, mpeg_process, mpeg_close
};
