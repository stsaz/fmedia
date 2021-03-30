/** fmedia: .mp3 write
2017,2021, Simon Zolin */

struct mpeg_out_conf_t {
	uint min_meta_size;
} mpeg_out_conf;

const ffpars_arg mpeg_out_conf_args[] = {
	{ "min_meta_size",	FFPARS_TINT,  FFPARS_DSTOFF(struct mpeg_out_conf_t, min_meta_size) },
};

int mpeg_out_config(ffpars_ctx *ctx)
{
	mpeg_out_conf.min_meta_size = 1000;
	ffpars_setargs(ctx, &mpeg_out_conf, mpeg_out_conf_args, FFCNT(mpeg_out_conf_args));
	return 0;
}

int mpeg_have_trkmeta(fmed_filt *d)
{
	fmed_trk_meta meta;
	ffmem_tzero(&meta);
	if (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta))
		return 1;
	return 0;
}

typedef struct mpeg_out {
	uint state;
	uint nframe;
	ffmpgw mpgw;
} mpeg_out;

void* mpeg_out_open(fmed_filt *d)
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

void mpeg_out_close(void *ctx)
{
	mpeg_out *m = ctx;
	ffmpg_wclose(&m->mpgw);
	ffmem_free(m);
}

int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d)
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

int mpeg_out_process(void *ctx, fmed_filt *d)
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
		d->mpg_lametag = 0;
		m->mpgw.lametag = 1;
		m->mpgw.fin = 1;
	}

	for (;;) {
		r = ffmpg_writeframe(&m->mpgw, (void*)d->data, d->datalen, &s);
		switch (r) {

		case FFMPG_RID32:
			dbglog(core, d->trk, NULL, "ID3v2: %L bytes"
				, s.len);
			goto data;

		case FFMPG_RID31:
			dbglog(core, d->trk, NULL, "ID3v1: %L bytes"
				, s.len);
			goto data;

		case FFMPG_RDATA:
			d->datalen = 0;
			dbglog(core, d->trk, NULL, "frame #%u: %L bytes"
				, m->nframe++, s.len);
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
	return FMED_RDATA;
}

const fmed_filter fmed_mpeg_output = {
	mpeg_out_open, mpeg_out_process, mpeg_out_close
};
