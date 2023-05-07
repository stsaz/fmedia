/** fmedia: zstd decompression filter
2023, Simon Zolin */

#include <fmedia.h>
#include <zstd/zstd-ff.h>

struct zstdr {
	zstd_decoder *zst;
	ffstr input;
	ffvec buf;
};

static void zstdr_close(void *ctx)
{
	struct zstdr *z = ctx;
	zstd_decode_free(z->zst);
	ffvec_free(&z->buf);
	ffmem_free(z);
}

static void* zstdr_open(fmed_track_info *ti)
{
	struct zstdr *z = ffmem_new(struct zstdr);
	zstd_dec_conf zc = {};
	zstd_decode_init(&z->zst, &zc);
	ffvec_alloc(&z->buf, 512*1024, 1);
	return z;
}

static int zstdr_process(void *ctx, fmed_track_info *ti)
{
	struct zstdr *z = ctx;

	if (ti->flags & FMED_FFWD)
		z->input = ti->data_in;

	zstd_buf in, out;
	zstd_buf_set(&in, z->input.ptr, z->input.len);
	zstd_buf_set(&out, z->buf.ptr, z->buf.cap);
	int r = zstd_decode(z->zst, &in, &out);
	ffstr_shift(&z->input, in.pos);
	if (r < 0) {
		errlog1(ti, "zstd_decode");
		return FMED_RERR;
	}

	if (out.pos == 0 && (ti->flags & FMED_FFIRST))
		return FMED_RDONE;

	if (out.pos == 0 && r > 0)
		return FMED_RMORE;

	ffstr_set(&ti->data_out, z->buf.ptr, out.pos);
	return FMED_RDATA;
}

const fmed_filter fmed_zstdr = { zstdr_open, zstdr_process, zstdr_close };
