/** fmedia: .mp3 copy
2017,2021, Simon Zolin */

typedef struct mpeg_copy {
	ffmpgcopy mpgcpy;
	ffstr in;
	uint64 until;
} mpeg_copy;

void* mpeg_copy_open(fmed_filt *d)
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

const fmed_filter fmed_mpeg_copy = {
	mpeg_copy_open, mpeg_copy_process, mpeg_copy_close
};
