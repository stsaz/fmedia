/** fmedia: .mp4 write
2021, Simon Zolin */

#include <avpack/mp4-write.h>

const fmed_queue *qu;

typedef struct mp4_out {
	uint state;
	mp4write mp;
	ffstr in;
	uint stmcopy :1;
} mp4_out;

static int mp4_out_addmeta(mp4_out *m, fmed_filt *d);

static void* mp4_out_create(fmed_filt *d)
{
	mp4_out *m = ffmem_new(mp4_out);
	if (m == NULL)
		return NULL;
	if (qu == NULL)
		qu = core->getmod("#queue.queue");
	return m;
}

static void mp4_out_free(void *ctx)
{
	mp4_out *m = ctx;
	mp4write_close(&m->mp);
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

		if (0 != mp4write_addtag(&m->mp, tag, *val)) {
			warnlog(core, d->trk, "mp4", "can't add tag: %S", &name);
		}
	}
	return 0;
}

/* Encoding process:
. Add encoder filter to the chain
. Get encoder config data
. Initialize MP4 output
. Wrap encoded audio data into MP4 */
static int mp4_out_encode(void *ctx, fmed_filt *d)
{
	mp4_out *m = ctx;
	int r;

	enum { I_INIT_ENC, I_INIT, I_MP4 };

	switch (m->state) {

	case I_INIT_ENC:
		if (ffsz_eq(d->datatype, "aac")) {
			m->state = I_INIT;

		} else if (ffsz_eq(d->datatype, "pcm")) {
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, "aac.encode"))
				return FMED_RERR;
			m->state = I_INIT;
			return FMED_RMORE;

		} else if (ffsz_eq(d->datatype, "mp4")) {
			m->state = I_INIT;
			m->stmcopy = 1;

		} else {
			errlog(core, d->trk, NULL, "unsupported input data format: %s", d->datatype);
			return FMED_RERR;
		}
		// fallthrough

	case I_INIT: {
		struct mp4_info info = {};
		ffstr_set(&info.conf, d->data, d->datalen);
		d->datalen = 0;
		info.fmt.bits = ffpcm_bits(d->audio.convfmt.format);
		info.fmt.channels = d->audio.convfmt.channels;
		info.fmt.rate = d->audio.convfmt.sample_rate;

		if (!m->stmcopy && (int64)d->audio.total != FMED_NULL
			&& d->duration_accurate)
			info.total_samples = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);

		if (FMED_NULL == (r = (int)fmed_getval("audio_frame_samples")))
			return FMED_RERR;
		info.frame_samples = r;

		if (FMED_NULL != (r = fmed_getval("audio_enc_delay")))
			info.enc_delay = r;
		if (FMED_NULL != (r = fmed_getval("audio_bitrate")))
			info.bitrate = r;

		if (0 != (r = mp4write_create_aac(&m->mp, &info))) {
			errlog(core, d->trk, NULL, "ffmp4_create_aac(): %s", mp4write_error(&m->mp));
			return FMED_RERR;
		}

		if (0 != mp4_out_addmeta(m, d))
			return FMED_RERR;

		m->state = I_MP4;
		break;
	}
	}

	if (d->flags & FMED_FLAST)
		m->mp.fin = 1;

	if (d->datalen != 0) {
		m->in = d->data_in;
		d->datalen = 0;
	}

	ffstr out;
	for (;;) {
		r = mp4write_process(&m->mp, &m->in, &out);
		switch (r) {
		case MP4WRITE_MORE:
			return FMED_RMORE;

		case MP4WRITE_SEEK:
			d->output.seek = m->mp.off;
			continue;

		case MP4WRITE_DATA:
			d->data_out = out;
			return FMED_RDATA;

		case MP4WRITE_DONE:
			d->outlen = 0;
			core->log(FMED_LOG_INFO, d->trk, NULL, "MP4: frames:%u, overhead: %.2F%%"
				, m->mp.frameno
				, (double)m->mp.mp4_size * 100 / (m->mp.mp4_size + m->mp.mdat_size));
			return FMED_RDONE;

		case MP4WRITE_ERROR:
			errlog(core, d->trk, NULL, "mp4write_process(): %s", mp4write_error(&m->mp));
			return FMED_RERR;
		}
	}
}

const fmed_filter mp4_output = {
	mp4_out_create, mp4_out_encode, mp4_out_free
};
