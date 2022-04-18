/** fmedia: .mp3 copy
2017,2021, Simon Zolin */

#include <format/mp3-copy-iface.h>

typedef struct mpeg_copy {
	ffmpgcopy mpgcpy;
	ffstr in;
	uint64 until;
	uint iframe;
} mpeg_copy;

void* mpeg_copy_open(fmed_filt *d)
{
	mpeg_copy *m = ffmem_new(mpeg_copy);
	if (m == NULL)
		return NULL;

	m->mpgcpy.options = MP3WRITE_ID3V2 | MP3WRITE_ID3V1;
	if ((int64)d->input.size != FMED_NULL)
		ffmpg_copy_setsize(&m->mpgcpy, d->input.size);

	m->until = (uint64)-1;
	if (d->audio.until != FMED_NULL) {
		m->until = d->audio.until;
		d->audio.until = FMED_NULL;
	}

	d->datatype = "mpeg";
	return m;
}

void mpeg_copy_close(void *ctx)
{
	mpeg_copy *m = ctx;
	ffmpg_copy_close(&m->mpgcpy);
	ffmem_free(m);
}

int mpeg_copy_process(void *ctx, fmed_filt *d)
{
	mpeg_copy *m = ctx;
	int r;
	ffstr out;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->datalen != 0) {
		m->in = d->data_in;
		d->datalen = 0;
	}

	for (;;) {

	r = ffmpg_copy(&m->mpgcpy, &m->in, &out);

	switch (r) {
	case FFMPG_RMORE:
		if (d->flags & FMED_FLAST) {
			ffmpg_copy_fin(&m->mpgcpy);
			continue;
		}
		return FMED_RMORE;

	case FFMPG_RHDR: {
		const struct mpeg1read_info *info = mpeg1read_info(&m->mpgcpy.rdr);
		d->audio.fmt.format = FFPCM_16;
		d->audio.fmt.sample_rate = info->sample_rate;
		d->audio.fmt.channels = info->channels;
		d->audio.bitrate = info->bitrate;
		d->audio.total = info->total_samples;
		d->audio.decoder = "MPEG";
		d->meta_block = 0;

		if ((int64)d->audio.seek != FMED_NULL) {
			int64 samples = ffpcm_samples(d->audio.seek, d->audio.fmt.sample_rate);
			ffmpg_copy_seek(&m->mpgcpy, samples);
			d->audio.seek = FMED_NULL;
			continue;
		}
		continue;
	}

	case FFMPG_RID32:
	case FFMPG_RID31:
		d->meta_block = 1;
		goto data;

	case FFMPG_RFRAME:
		d->audio.pos = mpeg1read_cursample(&m->mpgcpy.rdr);
		if (ffpcm_time(d->audio.pos, d->audio.fmt.sample_rate) >= m->until) {
			dbglog(core, d->trk, NULL, "reached time %Ums", m->until);
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
		core->log(FMED_LOG_INFO, d->trk, NULL, "MPEG: frames:%u", (int)mp3write_frames(&m->mpgcpy.writer));
		d->outlen = 0;
		return FMED_RLASTOUT;

	case FFMPG_RWARN:
		warnlog(core, d->trk, NULL, "near sample %U: ffmpg_copy(): %s"
			, mpeg1read_cursample(&m->mpgcpy.rdr), ffmpg_copy_errstr(&m->mpgcpy));
		continue;

	default:
		errlog(core, d->trk, "mpeg", "ffmpg_copy() failed: %s", ffmpg_copy_errstr(&m->mpgcpy));
		return FMED_RERR;
	}
	}

data:
	d->out = out.ptr,  d->outlen = out.len;
	dbglog(core, d->trk, "mpeg", "output: frame#%u  size:%L"
		, m->iframe++, out.len);
	return FMED_RDATA;
}

const fmed_filter mp3_copy = {
	mpeg_copy_open, mpeg_copy_process, mpeg_copy_close
};
