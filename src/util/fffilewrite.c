/**
Copyright (c) 2020 Simon Zolin
*/

#include "filewrite.h"
#include "string.h"
#include <FFOS/dir.h>
#include <FFOS/timer.h>

#define dbglog(f, fmt, ...) \
do { \
	if (f->conf.log_debug) \
		fw_log(f, FFFILEWRITE_LOG_DBG, fmt, __VA_ARGS__); \
} while (0)

#define syserrlog(f, fmt, ...)  fw_log(f, _FFFILEWRITE_LOG_SYSERR, "%s: " fmt, (f)->name, __VA_ARGS__)


static void fw_writedone(fffilewrite *f, uint64 off, size_t written);

struct buf_s {
	size_t len;
	void *ptr;
	uint64 off;
};

struct fffilewrite {
	fffilewrite_conf conf;
	fffilewrite_stat stat;
	fffd fd;
	char *name;
	uint completed :1;
	uint nfy_user :1;
	ffthpool_task *iotask; // AIO task object
	uint aio_done;
	fflock lk;
	uint state; // enum FW_ST

	// buffering:
	int locked; // buffer index used in AIO;  -1: no AIO is pending
	uint buf_r; // read buffer index
	uint buf_w; // write buffer index
	struct buf_s bufs[2]; // bufferred data
	uint64 cur_off; // current file offset
	uint cur_len; // current buffer length

	// preallocation:
	uint64 prealloc_size; // preallocated size
	uint64 size; // file size
};

enum FW_ST {
	FW_OK,
	FW_ASYNC,
	FW_CLOSED,
};

void fffilewrite_setconf(fffilewrite_conf *conf)
{
	ffmem_tzero(conf);
	conf->kq = FF_BADFD;
	conf->align = 4096;
	conf->bufsize = 64 * 1024;
	conf->nbufs = 2;
	conf->create = 1;
	conf->mkpath = 1;
	conf->prealloc = 128 * 1024;
	conf->prealloc_grow = 1;
}

static void fw_log(fffilewrite *f, uint level, const char *fmt, ...)
{
	if (f->conf.log == NULL)
		return;

	char buf[4096];
	ffstr s;
	ffstr_set(&s, buf, 0);

	va_list va;
	va_start(va, fmt);
	ffstr_addfmtv(&s, sizeof(buf), fmt, va);
	va_end(va);

	if (level == _FFFILEWRITE_LOG_SYSERR) {
		ffstr_addfmt(&s, sizeof(buf), ": %E", fferr_last());
		level = FFFILEWRITE_LOG_ERR;
	}

	f->conf.log(f->conf.udata, level, s);
}

fffilewrite* fffilewrite_create(const char *fn, fffilewrite_conf *conf)
{
	if (conf->nbufs == 0 || conf->nbufs > FFCNT(((fffilewrite*)NULL)->bufs))
		return NULL;

	fffilewrite *f = ffmem_new(fffilewrite);
	if (f == NULL)
		return NULL;
	f->conf = *conf;
	if (NULL == (f->name = ffsz_alcopyz(fn))) {
		syserrlog(f, "mem alloc", 0);
		goto end;
	}

	for (uint i = 0;  i != f->conf.nbufs;  i++) {
		void *b;
		if (NULL == (b = ffmem_align(f->conf.bufsize, f->conf.align))) {
			syserrlog(f, "mem alloc", 0);
			goto end;
		}
		f->bufs[i].ptr = b;
	}

	f->locked = -1;
	f->fd = FF_BADFD;
	return f;

end:
	fffilewrite_free(f);
	return NULL;
}

void fffilewrite_free(fffilewrite *f)
{
	if (f == NULL)
		return;

	if (f->fd != FF_BADFD) {
		if (0 != fffile_trunc(f->fd, f->size))
			syserrlog(f, "fffile_trunc", 0);

		if (0 != fffile_close(f->fd))
			syserrlog(f, "%s", fffile_close_S, 0);
		f->fd = FF_BADFD;

		if (!f->completed && f->conf.del_on_err) {
			if (0 != fffile_rm(f->name))
				syserrlog(f, "%s", fffile_rm_S);
			else
				dbglog(f, "removed file", 0);
		}
	}

	ffbool ret = 0;
	fflk_lock(&f->lk);
	if (f->state == FW_ASYNC) {
		f->state = FW_CLOSED;
		ret = 1;
	}
	fflk_unlock(&f->lk);
	if (ret)
		return; //wait until AIO is completed

	ffmem_free(f->name);
	for (uint i = 0;  i != f->conf.nbufs;  i++) {
		ffmem_alignfree(f->bufs[i].ptr);
	}
	ffthpool_task_free(f->iotask);
	ffmem_free(f);
}

/** Open/create file;  create file path. */
static int fw_open(fffilewrite *f)
{
	uint errmask = 0;
	uint flags = 0;
	if (f->conf.create)
		flags = (f->conf.overwrite) ? FFO_CREATE : FFO_CREATENEW;
	else
		f->conf.prealloc = 0;
	flags |= f->conf.oflags;
	flags |= FFO_WRONLY;
	while (FF_BADFD == (f->fd = fffile_open(f->name, flags))) {
		if (fferr_nofile(fferr_last()) && f->conf.mkpath) {
			if (ffbit_set32(&errmask, 0))
				goto err;
			if (0 != ffdir_make_path(f->name, 0)) {
				syserrlog(f, "%s", ffdir_make_S);
				goto err;
			}
		} else {
			syserrlog(f, "%s", fffile_open_S);
			goto err;
		}
	}

	return 0;

err:
	return FFFILEWRITE_RERR;
}

static ffbool buf_empty(fffilewrite *f)
{
	return f->buf_r == f->buf_w
		&& f->bufs[f->buf_r].len == 0;
}

static ffbool buf_full(fffilewrite *f)
{
	return f->buf_r == f->buf_w
		&& f->bufs[f->buf_w].len != 0;
}

/** Increment and reset to 0 on reaching the limit. */
#define ffint_cycleinc(n, lim)  (((n) + 1) % (lim))

/** Store data in internal buffer. */
static size_t fw_buf_add(fffilewrite *f, ffstr data, int64 off, uint flags)
{
	struct buf_s *buf = &f->bufs[f->buf_w];

	if (off == -1)
		off = f->cur_off;
	else {
		if (f->cur_off != (uint64)off && f->cur_len != 0) {
			data.len = 0;
			flags |= FFFILEWRITE_FFLUSH;
		}
		f->cur_off = off;
	}

	if (buf_full(f))
		return 0;

	if (f->cur_len == 0) {
		if (data.len == 0)
			return 0;
		buf->off = off;
	}

	ffstr tmp;
	ffstr_set(&tmp, buf->ptr, f->cur_len);
	size_t n = ffstr_add2(&tmp, f->conf.bufsize, &data);
	f->cur_len = tmp.len;
	f->cur_off += n;
	if (!(f->cur_len == f->conf.bufsize
		|| (flags & FFFILEWRITE_FFLUSH))) {
		f->stat.nmwrite++;
		return n;
	}

	buf->len = f->cur_len;
	f->cur_len = 0;
	f->buf_w = ffint_cycleinc(f->buf_w, f->conf.nbufs);
	return n;
}

static int fw_buf_lock_get(fffilewrite *f, struct buf_s *dst)
{
	if (buf_empty(f))
		return FFFILEWRITE_RERR;
	if (f->locked != -1)
		return FFFILEWRITE_RASYNC;

	struct buf_s *buf = &f->bufs[f->buf_r];
	*dst = *buf;
	f->locked = f->buf_r;
	return 0;
}

static void fw_buf_unlock(fffilewrite *f)
{
	struct buf_s *buf = &f->bufs[f->buf_r];
	buf->len = 0;
	buf->off = 0;
	f->buf_r = ffint_cycleinc(f->buf_r, f->conf.nbufs);
	f->locked = -1;
}

/** Preallocate disk space. */
static void fw_prealloc(fffilewrite *f, struct buf_s data)
{
	uint64 roff = data.off + data.len;
	if (f->conf.prealloc == 0 || roff <= f->prealloc_size)
		return;

	uint64 n = ff_align_ceil(roff, f->conf.prealloc);
	if (0 != fffile_trunc(f->fd, n))
		return;

	if (f->conf.prealloc_grow)
		f->conf.prealloc *= 2;

	f->prealloc_size = n;
	f->stat.nprealloc++;
	dbglog(f, "prealloc: %U", n);
}

/** Write data to disk. */
static int fw_write(fffilewrite *f, struct buf_s d)
{
	fftime t1 = {}, t2;
	if (f->conf.log_debug)
		ffclk_gettime(&t1);

	ssize_t n = fffile_pwrite(f->fd, d.ptr, d.len, d.off);

	if (f->conf.log_debug) {
		ffclk_gettime(&t2);
		fftime_sub(&t2, &t1);
		dbglog(f, "write result:%D  offset:%xU  (%uus)"
			, (int64)n, d.off, fftime_mcs(&t2));
	}

	if (n < 0) {
		syserrlog(f, "%s", fffile_write_S);
		return FFFILEWRITE_RERR;
	}
	FF_ASSERT((size_t)n == d.len);
	fw_writedone(f, d.off, n);
	return 0;
}

static void fw_writedone(fffilewrite *f, uint64 off, size_t written)
{
	f->stat.nfwrite++;
	dbglog(f, "%s: written %L bytes at offset %xU"
		, f->name, written, off);
	f->size = ffmax(f->size, off + written);
}

struct fw_task {
	ffstr buf;
	uint64 off;
	fffd fd;
	int error;
	ssize_t result;
};

/** Called within thread pool's worker. */
static void fw_aio(ffthpool_task *t)
{
	fffilewrite *f = t->udata;
	struct fw_task *ext = (void*)t->ext;

	fftime t1 = {}, t2;
	if (f->conf.log_debug)
		ffclk_gettime(&t1);

	ext->result = fffile_pwrite(ext->fd, ext->buf.ptr, ext->buf.len, ext->off);
	ext->error = fferr_last();

	if (f->conf.log_debug) {
		ffclk_gettime(&t2);
		fftime_sub(&t2, &t1);
		dbglog(f, "write result:%D  offset:%xU  error:%d  (%uus)"
			, (int64)ext->result, ext->off, ext->error, fftime_mcs(&t2));
	}

	f->aio_done = 1;

	/* Handling a close event from user while AIO is pending:
	...
	set FW_ASYNC, add asynchronous task
	user calls free()
	FW_ASYNC: free() sets FW_CLOSED and waits until aio() is called or unblocked
	FW_CLOSED: aio() destroys the object
	FW_OK:
	  aio() is calling the user function;  free() is waiting
	  aio() has finished calling the user function;  free() continues execution
	*/
	fflk_lock(&f->lk);
	if (f->state == FW_CLOSED) {
		// user has closed the object
		fflk_unlock(&f->lk);
		fffilewrite_free(f);
		return;
	}
	FF_ASSERT(f->state == FW_ASYNC);
	f->state = FW_OK;
	if (f->nfy_user) {
		f->nfy_user = 0;
		f->conf.onwrite(f->conf.udata);
	}
	fflk_unlock(&f->lk);
}

/** Add buffer write task to a thread pool. */
static int fw_thpool_write(fffilewrite *f, struct buf_s chunk)
{
	ffthpool_task *t;
	if (NULL == (t = ffthpool_task_new(sizeof(struct fw_task))))
		return FFFILEWRITE_RERR;

	struct fw_task *ext = (void*)t->ext;
	ext->fd = f->fd;
	ffstr_set2(&ext->buf, &chunk);
	ext->off = chunk.off;

	t->handler = &fw_aio;
	t->udata = f;
	FF_ASSERT(f->iotask == NULL);
	f->iotask = t;
	f->state = FW_ASYNC;
	dbglog(f, "adding file write task to thread pool: offset:%xU", chunk.off);
	if (0 != ffthpool_add(f->conf.thpool, t)) {
		f->state = FW_OK;
		syserrlog(f, "ffthpool_add", 0);
		ffthpool_task_free(t);
		f->iotask = NULL;
		return FFFILEWRITE_RERR;
	}
	f->stat.nasync++;
	return 0;
}

static int fw_thpool_result(fffilewrite *f)
{
	int r = FFFILEWRITE_RERR;
	ffthpool_task *t = f->iotask;
	struct fw_task *ext = (void*)t->ext;

	fw_buf_unlock(f);

	if (ext->result < 0) {
		fferr_set(ext->error);
		syserrlog(f, "%s", fffile_write_S);
		goto end;
	}

	FF_ASSERT((size_t)ext->result == ext->buf.len);
	fw_writedone(f, ext->off, ext->result);
	r = 0;

end:
	ffthpool_task_free(f->iotask);
	f->iotask = NULL;
	return r;
}

ssize_t fffilewrite_write(fffilewrite *f, ffstr data, int64 off, uint flags)
{
	int r;
	ssize_t wr;
	struct buf_s chunk;
	uint64 allwr = 0;

	if (f->fd == FF_BADFD && 0 != (r = fw_open(f)))
		return r;

	for (;;) {

		if (f->aio_done) {
			f->aio_done = 0;
			if (0 != (r = fw_thpool_result(f)))
				return r;
		}

		wr = fw_buf_add(f, data, off, flags);
		allwr += wr;

		if (0 != (r = fw_buf_lock_get(f, &chunk))) {
			if (r == FFFILEWRITE_RASYNC && allwr == 0) {
				fflk_lock(&f->lk);
				if (f->state == FW_ASYNC)
					f->nfy_user = 1;
				else if (f->state == FW_OK) {
					FF_ASSERT(f->aio_done);
					fflk_unlock(&f->lk);
					ffstr_shift(&data, wr);
					off = -1;
					continue;
				}
				fflk_unlock(&f->lk);
				return FFFILEWRITE_RASYNC;
			}

			if (r == FFFILEWRITE_RERR
				&& data.len == 0
				&& (flags & FFFILEWRITE_FFLUSH))
				f->completed = 1;

			return allwr;
		}

		fw_prealloc(f, chunk);

		if (f->conf.thpool != NULL) {
			r = fw_thpool_write(f, chunk);
			if (r != 0)
				return r;
		} else {
			r = fw_write(f, chunk);
			if (r != 0)
				return r;
			fw_buf_unlock(f);
		}

		ffstr_shift(&data, wr);
		off = -1;
	}
}

fffd fffilewrite_fd(fffilewrite *f)
{
	return f->fd;
}

void fffilewrite_getstat(fffilewrite *f, fffilewrite_stat *stat)
{
	*stat = f->stat;
}
