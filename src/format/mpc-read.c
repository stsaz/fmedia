/** fmedia: .mpc reader
2017, Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>
#include <avpack/mpc-read.h>

extern const fmed_core *core;
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

struct mpc {
	mpcread mpc;
	ffstr in;
	uint64 frno;
	void *trk;
	uint sample_rate;
};

static void mpc_log(void *udata, const char *fmt, va_list va)
{
	struct mpc *m = udata;
	fmed_dbglogv(core, m->trk, NULL, fmt, va);
}

static void* mpc_open(fmed_filt *d)
{
	struct mpc *m;
	if (NULL == (m = ffmem_new(struct mpc)))
		return NULL;
	m->trk = d->trk;
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

	if (d->datalen != 0) {
		ffstr_set(&m->in, d->data, d->datalen);
		d->datalen = 0;
	}

	if (d->seek_req && (int64)d->audio.seek != FMED_NULL && m->sample_rate != 0) {
		d->seek_req = 0;
		mpcread_seek(&m->mpc, ffpcm_samples(d->audio.seek, m->sample_rate));
		dbglog1(d->trk, "seek: %Ums", d->audio.seek);
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
	d->audio.pos = mpcread_cursample(&m->mpc);
	dbglog(core, d->trk, NULL, "frame#%U passing %L bytes at position #%U"
		, ++m->frno, blk.len, d->audio.pos);
	d->out = blk.ptr,  d->outlen = blk.len;
	return FMED_RDATA;
}

const fmed_filter mpc_input = {
	mpc_open, mpc_process, mpc_close
};
