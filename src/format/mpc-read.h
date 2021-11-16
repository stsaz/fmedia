/** fmedia: .mpc reader
2017, Simon Zolin */

#include <format/mmtag.h>
#include <avpack/mpc-read.h>

struct mpc {
	mpcread mpc;
	ffstr in;
	int64 aseek;
	uint64 frno;
	void *trk;
	uint sample_rate;
	uint seeking :1;
};

static void mpc_log(void *udata, ffstr msg)
{
	struct mpc *m = udata;
	dbglog1(m->trk, "%S", &msg);
}

static void* mpc_open(fmed_filt *d)
{
	struct mpc *m;
	if (NULL == (m = ffmem_new(struct mpc)))
		return NULL;
	m->trk = d->trk;
	m->aseek = -1;
	uint64 tsize = 0;
	if ((int64)d->input.size != FMED_NULL)
		tsize = d->input.size;
	mpcread_open(&m->mpc, tsize);
	m->mpc.log = mpc_log;
	m->mpc.udata = m;
	return m;
}

static void mpc_close(void *ctx)
{
	struct mpc *m = ctx;
	mpcread_close(&m->mpc);
	ffmem_free(m);
}

static int mpc_process(void *ctx, fmed_filt *d)
{
	struct mpc *m = ctx;
	int r;
	ffstr blk;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->codec_err) {
		d->codec_err = 0;
	}

	if (d->datalen != 0) {
		ffstr_set(&m->in, d->data, d->datalen);
		d->datalen = 0;
	}

	if ((int64)d->audio.seek >= 0)
		m->aseek = d->audio.seek;
	if (m->aseek >= 0 && !m->seeking && m->sample_rate != 0) {
		mpcread_seek(&m->mpc, ffpcm_samples(m->aseek, m->sample_rate));
		m->seeking = 1;
	}

	for (;;) {
		r = mpcread_process(&m->mpc, &m->in, &blk);

		switch (r) {

		case MPCREAD_HEADER: {
			const struct mpcread_info *info = mpcread_info(&m->mpc);
			d->audio.fmt.sample_rate = info->sample_rate;
			m->sample_rate = info->sample_rate;
			d->audio.fmt.channels = info->channels;

			if ((int64)d->input.size != FMED_NULL)
				d->audio.bitrate = ffpcm_brate(d->input.size, info->total_samples, info->sample_rate);

			d->audio.total = info->total_samples;
			d->audio.decoder = "Musepack";

			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, "mpc.decode"))
				return FMED_RERR;

			d->data_out = blk;
			return FMED_RDATA;
		}

		case MPCREAD_MORE:
			return FMED_RMORE;

		case MPCREAD_SEEK:
			d->input.seek = mpcread_offset(&m->mpc);
			return FMED_RMORE;

		case MPCREAD_TAG: {
			ffstr name, val;
			int r = mpcread_tag(&m->mpc, &name, &val);
			if (r != 0)
				ffstr_setz(&name, ffmmtag_str[r]);
			dbglog(core, d->trk, NULL, "tag: %S: %S", &name, &val);
			d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
			continue;
		}

		case MPCREAD_DATA:
			goto data;

		case MPCREAD_DONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case MPCREAD_WARN:
			warnlog(core, d->trk, "mpc", "mpcread_process(): %s.  Offset: %U"
				, mpcread_error(&m->mpc), mpcread_offset(&m->mpc));
			continue;
		case MPCREAD_ERROR:
			errlog(core, d->trk, "mpc", "mpcread_process(): %s.  Offset: %U"
				, mpcread_error(&m->mpc), mpcread_offset(&m->mpc));
			return FMED_RERR;
		}
	}

data:
	if (m->seeking) {
		m->seeking = 0;
		m->aseek = -1;
	}
	d->audio.pos = mpcread_cursample(&m->mpc);
	dbglog(core, d->trk, NULL, "frame#%U passing %L bytes at position #%U"
		, ++m->frno, blk.len, d->audio.pos);
	d->out = blk.ptr,  d->outlen = blk.len;
	return FMED_RDATA;
}

static const fmed_filter mpc_input = {
	mpc_open, mpc_process, mpc_close
};
