/** fmedia: zstd compression filter
2023, Simon Zolin */

#include <fmedia.h>
#include <zstd/zstd-ff.h>

struct zstdw {
	zstd_encoder *zst;
	ffstr input;
	ffvec buf;
};

static void zstdw_close(void *ctx)
{
	struct zstdw *z = ctx;
	zstd_encode_free(z->zst);
	ffvec_free(&z->buf);
	ffmem_free(z);
}

static void* zstdw_open(fmed_track_info *ti)
{
	struct zstdw *z = ffmem_new(struct zstdw);
	zstd_enc_conf zc = {};
	zc.level = 1;
	zc.workers = 1;
	zstd_encode_init(&z->zst, &zc);
	ffvec_alloc(&z->buf, 512*1024, 1);
	return z;
}

static int zstdw_process(void *ctx, fmed_track_info *ti)
{
	struct zstdw *z = ctx;

	if (ti->flags & FMED_FFWD)
		z->input = ti->data_in;

	zstd_buf in, out;
	zstd_buf_set(&in, z->input.ptr, z->input.len);
	zstd_buf_set(&out, z->buf.ptr, z->buf.cap);
	uint flags = 0;
	if (ti->flags & FMED_FFIRST)
		flags |= ZSTD_FFINISH;
	int r = zstd_encode(z->zst, &in, &out, flags);
	ffstr_shift(&z->input, in.pos);
	if (r < 0) {
		errlog1(ti, "zstd_encode");
		return FMED_RERR;
	}

	if (out.pos == 0 && (ti->flags & FMED_FFIRST))
		return FMED_RDONE;

	if (out.pos == 0 && r > 0)
		return FMED_RMORE;

	ffstr_set(&ti->data_out, z->buf.ptr, out.pos);
	return FMED_RDATA;
}

const fmed_filter fmed_zstdw = { zstdw_open, zstdw_process, zstdw_close };
