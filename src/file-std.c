/** File std input/output.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>


extern const fmed_core *core;

#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "file", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "file", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "file", __VA_ARGS__)


//STDIN
static void* file_stdin_open(fmed_filt *d);
static int file_stdin_read(void *ctx, fmed_filt *d);
static void file_stdin_close(void *ctx);
const fmed_filter file_stdin = {
	&file_stdin_open, &file_stdin_read, &file_stdin_close
};

//STDOUT
static void* file_stdout_open(fmed_filt *d);
static int file_stdout_write(void *ctx, fmed_filt *d);
static void file_stdout_close(void *ctx);
const fmed_filter file_stdout = {
	&file_stdout_open, &file_stdout_write, &file_stdout_close
};


typedef struct stdin_ctx {
	fffd fd;
	ffarr buf;
} stdin_ctx;

static void* file_stdin_open(fmed_filt *d)
{
	stdin_ctx *f = ffmem_tcalloc1(stdin_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdin;

	if (NULL == ffarr_alloc(&f->buf, 64 * 1024)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdin_close(f);
	return NULL;
}

static void file_stdin_close(void *ctx)
{
	stdin_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdin_read(void *ctx, fmed_filt *d)
{
	stdin_ctx *f = ctx;
	ssize_t r;

	if ((int64)d->input.seek != FMED_NULL) {
		errlog(d->trk, "can't seek on stdin.  offset:%U", d->input.seek);
		return FMED_RERR;
	}

	r = ffstd_fread(f->fd, f->buf.ptr, f->buf.cap);
	if (r == 0) {
		d->outlen = 0;
		return FMED_RDONE;
	} else if (r < 0) {
		syserrlog(d->trk, "%s", fffile_read_S);
		return FMED_RERR;
	}

	dbglog(d->trk, "read %L bytes from stdin"
		, r);
	d->out = f->buf.ptr, d->outlen = r;
	return FMED_RDATA;
}


typedef struct stdout_ctx {
	fffd fd;
	ffarr buf;
	uint64 fsize;

	struct {
		uint nmwrite;
		uint nfwrite;
	} stat;
} stdout_ctx;

static void* file_stdout_open(fmed_filt *d)
{
	stdout_ctx *f = ffmem_tcalloc1(stdout_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdout;

	if (NULL == ffarr_alloc(&f->buf, 64 * 1024)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdout_close(f);
	return NULL;
}

static void file_stdout_close(void *ctx)
{
	stdout_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdout_writedata(stdout_ctx *f, const char *data, size_t len, fmed_filt *d)
{
	size_t r;
	r = fffile_write(f->fd, data, len);
	if (r != len) {
		syserrlog(d->trk, "%s", fffile_write_S);
		return -1;
	}
	f->stat.nfwrite++;

	dbglog(d->trk, "written %L bytes at offset %U (%L pending)", r, f->fsize, d->datalen);
	f->fsize += r;
	return r;
}

static int file_stdout_write(void *ctx, fmed_filt *d)
{
	stdout_ctx *f = ctx;
	ssize_t r;
	ffstr dst;

	if ((int64)d->output.seek != FMED_NULL) {

		if (f->buf.len != 0) {
			if (-1 == file_stdout_writedata(f, f->buf.ptr, f->buf.len, d))
				return FMED_RERR;
			f->buf.len = 0;
		}

		errlog(d->trk, "can't seek on stdout.  offset:%U", d->output.seek);
		return FMED_RERR;
	}

	for (;;) {

		r = ffbuf_add(&f->buf, d->data, d->datalen, &dst);
		d->data += r;
		d->datalen -= r;
		if (dst.len == 0) {
			f->stat.nmwrite++;
			if (!(d->flags & FMED_FLAST) || f->buf.len == 0)
				break;
			ffstr_set(&dst, f->buf.ptr, f->buf.len);
		}

		if (-1 == file_stdout_writedata(f, dst.ptr, dst.len, d))
			return FMED_RERR;

		if (d->datalen == 0)
			break;
	}

	if (d->flags & FMED_FLAST) {
		return FMED_RDONE;
	}

	return FMED_ROK;
}
