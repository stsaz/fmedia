/** fmedia: .mp3 write
2017,2021, Simon Zolin */

#include <avpack/mp3-write.h>

struct mpeg_out_conf_t {
	uint min_meta_size;
} mpeg_out_conf;

const fmed_conf_arg mpeg_out_conf_args[] = {
	{ "min_meta_size",	FMC_INT32,  FMC_O(struct mpeg_out_conf_t, min_meta_size) },
	{}
};

int mpeg_out_config(fmed_conf_ctx *ctx)
{
	mpeg_out_conf.min_meta_size = 1000;
	fmed_conf_addctx(ctx, &mpeg_out_conf, mpeg_out_conf_args);
	return 0;
}

int mpeg_have_trkmeta(fmed_filt *d)
{
	fmed_trk_meta meta;
	ffmem_zero_obj(&meta);
	if (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta))
		return 1;
	return 0;
}

typedef struct mpeg_out {
	uint state;
	uint nframe;
	uint flags;
	ffstr in;
	mp3write mpgw;
} mpeg_out;

void* mpeg_out_open(fmed_filt *d)
{
	if (ffsz_eq(d->datatype, "mpeg") && !mpeg_have_trkmeta(d))
		return FMED_FILT_SKIP; // mpeg.copy is used in this case

	mpeg_out *m = ffmem_new(mpeg_out);
	if (m == NULL)
		return NULL;
	mp3write_create(&m->mpgw);
	m->mpgw.id3v2_min_size = mpeg_out_conf.min_meta_size;
	return m;
}

void mpeg_out_close(void *ctx)
{
	mpeg_out *m = ctx;
	mp3write_close(&m->mpgw);
	ffmem_free(m);
}

int mpeg_out_addmeta(mpeg_out *m, fmed_filt *d)
{
	ssize_t r;
	fmed_trk_meta meta;
	ffmem_zero_obj(&meta);
	meta.flags = FMED_QUE_UNIQ;

	while (0 == d->track->cmd2(d->trk, FMED_TRACK_META_ENUM, &meta)) {
		if (-1 == (r = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), meta.name.ptr, meta.name.len))
			|| r == MMTAG_VENDOR)
			continue;

		if (0 != mp3write_addtag(&m->mpgw, r, meta.val)) {
			warnlog(core, d->trk, "mpeg", "can't add tag: %S", &meta.name);
		}
	}
	return 0;
}

int mpeg_out_process(void *ctx, fmed_filt *d)
{
	mpeg_out *m = ctx;
	int r;

	switch (m->state) {
	case 0:
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

	if (d->flags & FMED_FLAST)
		m->flags |= MP3WRITE_FLAST;
	if (d->mpg_lametag)
		m->flags |= MP3WRITE_FLAMEFRAME;

	ffstr out;
	if (d->data_in.len != 0) {
		m->in = d->data_in;
		d->data_in.len = 0;
	}

	for (;;) {
		r = mp3write_process(&m->mpgw, &m->in, &out, m->flags);
		switch (r) {

		case MP3WRITE_DATA:
			dbglog(core, d->trk, NULL, "frame #%u: %L bytes"
				, m->nframe++, out.len);
			goto data;

		case MP3WRITE_MORE:
			return FMED_RMORE;

		case MP3WRITE_SEEK:
			d->output.seek = mp3write_offset(&m->mpgw);
			continue;

		case MP3WRITE_DONE:
			d->outlen = 0;
			return FMED_RDONE;

		default:
			errlog(core, d->trk, "mpeg", "mp3write_process() failed");
			return FMED_RERR;
		}
	}

data:
	d->data_out = out;
	return FMED_RDATA;
}

const fmed_filter mp3_output = {
	mpeg_out_open, mpeg_out_process, mpeg_out_close
};
