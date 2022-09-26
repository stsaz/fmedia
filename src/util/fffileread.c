/**
Copyright (c) 2019 Simon Zolin
*/

#include "fileread.h"
#include <FFOS/timer.h>
#include "ffos-compat/asyncio.h"
#include <ffbase/slice.h>


static int fr_read_off(fffileread *f, uint64 off);
static int fr_read(fffileread *f);


struct buf {
	size_t len;
	char *ptr;
	uint64 offset;
};

struct fffileread {
	fffd fd;
	ffaio_filetask aio;
	fflock lk;
	uint state; //enum FI_ST
	uint64 eof; // end-of-file position set after the last block has been read
	uint64 async_off; // last user request's offset for which async operation is scheduled
	ffthpool_task *iotask;
	uint nfy_user :1;

	ffslice bufs; //struct buf[]
	uint wbuf;
	uint locked;

	fffileread_conf conf;
	struct fffileread_stat stat;
};

#define dbglog(f, fmt, ...) \
do { \
	if (f->conf.log_debug) \
		fr_log(f, FFFILEREAD_LOG_DBG, fmt, __VA_ARGS__); \
} while (0)

#define errlog(f, fmt, ...)  fr_log(f, FFFILEREAD_LOG_ERR, fmt, __VA_ARGS__)
#define syserrlog(f, fmt, ...)  fr_log(f, _FFFILEREAD_LOG_SYSERR, fmt, __VA_ARGS__)

static void fr_log(fffileread *f, uint level, const char *fmt, ...)
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

	if (level == _FFFILEREAD_LOG_SYSERR) {
		ffstr_addfmt(&s, sizeof(buf), ": %E", fferr_last());
		level = FFFILEREAD_LOG_ERR;
	}

	f->conf.log(f->conf.udata, level, s);
}

/** Create buffers aligned to system pagesize. */
static int bufs_create(fffileread *f, const fffileread_conf *conf)
{
	if (NULL == ffslice_zallocT(&f->bufs, conf->nbufs, struct buf))
		goto err;
	f->bufs.len = conf->nbufs;
	struct buf *b;
	FFSLICE_WALK_T(&f->bufs, b, struct buf) {
		if (NULL == (b->ptr = ffmem_align(conf->bufsize, conf->bufalign)))
			goto err;
		b->offset = (uint64)-1;
	}
	return 0;

err:
	syserrlog(f, "%s", ffmem_alloc_S);
	return -1;
}

static void bufs_free(ffslice *bufs)
{
	struct buf *b;
	FFSLICE_WALK_T(bufs, b, struct buf) {
		ffmem_alignfree(b->ptr);
	}
	ffslice_free(bufs);
}

/** Find buffer containing file offset. */
static struct buf* bufs_find(fffileread *f, uint64 offset)
{
	struct buf *b;
	FFSLICE_WALK_T(&f->bufs, b, struct buf) {
		if (b->offset <= offset && offset < b->offset + b->len)
			return b;
	}
	return NULL;
}

/** Prepare buffer for reading. */
static void buf_prepread(struct buf *b, uint64 off)
{
	b->len = 0;
	b->offset = off;
}


enum R {
	R_ASYNC,
	R_DONE,
	R_DATA,
	R_ERR,
};

enum FI_ST {
	FI_OK,
	FI_ERR,
	FI_ASYNC,
	FI_EOF,
	FI_CLOSED,
};

void fffileread_setconf(fffileread_conf *conf)
{
	ffmem_tzero(conf);
	conf->kq = FF_BADFD;
	conf->oflags = FFO_RDONLY;
	conf->bufsize = 64 * 1024;
	conf->nbufs = 1;
	conf->bufalign = 4 * 1024;
}

fffileread* fffileread_create(const char *fn, fffileread_conf *conf)
{
	if (conf->nbufs == 0
		|| conf->bufalign == 0
		|| conf->bufsize == 0
		|| conf->bufalign != ff_align_power2(conf->bufalign)
		|| conf->bufsize != ff_align_floor2(conf->bufsize, conf->bufalign)
		|| (conf->directio && conf->onread == NULL))
		return NULL;

	fffileread *f;
	if (NULL == (f = ffmem_new(fffileread)))
		return NULL;
	f->fd = FF_BADFD;
	f->async_off = (uint64)-1;
	f->eof = (uint64)-1;
	f->conf.udata = conf->udata;
	f->conf.log = conf->log;

	if (0 != bufs_create(f, conf))
		goto err;

	uint flags = conf->oflags;

	if (conf->kq != FF_BADFD)
		flags |= (conf->directio) ? FFO_DIRECT : 0;

	while (FF_BADFD == (f->fd = fffile_open(fn, flags))) {

#ifdef FF_LINUX
		if (fferr_last() == EINVAL && (flags & FFO_DIRECT)) {
			flags &= ~FFO_DIRECT;
			continue;
		}
#endif

		syserrlog(f, "%s", fffile_open_S);
		goto err;
	}

	ffaio_finit(&f->aio, f->fd, f);
	if (0 != ffaio_fattach(&f->aio, conf->kq, !!(flags & FFO_DIRECT))) {
		syserrlog(f, "%s: %s", ffkqu_attach_S, fn);
		goto err;
	}
	f->conf = *conf;

	conf->directio = !!(flags & FFO_DIRECT);
	return f;

err:
	fffileread_free(f);
	return NULL;
}

void fffileread_free_ex(fffileread *f, ffuint flags)
{
	if (flags & 1)
		f->fd = FFFILE_NULL;
	FF_SAFECLOSE(f->fd, FF_BADFD, fffile_close);

	ffbool ret = 0;
	fflk_lock(&f->lk);
	if (f->state == FI_ASYNC) {
		f->state = FI_CLOSED;
		ret = 1;
	}
	fflk_unlock(&f->lk);
	if (ret)
		return; //wait until AIO is completed

	bufs_free(&f->bufs);
	ffthpool_task_free(f->iotask);
	ffmem_free(f);
}

struct fr_task {
	ffstr buf;
	uint64 off;
	fffd fd;
	int error;
	ssize_t result;
};

/** Called within thread pool's worker. */
static void fr_aio(ffthpool_task *t)
{
	fffileread *f = t->udata;
	struct fr_task *ext = (void*)t->ext;
	fftime t1 = {}, t2;
	if (f->conf.log_debug)
		t1 = fftime_monotonic();
	ext->result = fffile_pread(ext->fd, ext->buf.ptr, ext->buf.len, ext->off);
	ext->error = fferr_last();
	if (f->conf.log_debug) {
		t2 = fftime_monotonic();
		fftime_sub(&t2, &t1);
		if (ext->result < 0) {
			dbglog(f, "read error:%d  offset:%xU  (%uus)"
				, ext->error, ext->off, fftime_mcs(&t2));
		} else {
			dbglog(f, "read result:%L  offset:%xU  (%uus)"
				, ext->result, ext->off, fftime_mcs(&t2));
		}
	}
	FF_ASSERT(t == f->iotask);
	FF_ASSERT(f->nfy_user);

	/* Handling a close event from user while AIO is pending:
	...
	set FI_ASYNC, add asynchronous task
	user calls free()
	FI_ASYNC: free() sets FI_CLOSED and waits until aio() is called or unblocked
	FI_CLOSED: aio() destroys the object
	FI_OK:
	  aio() is calling the user function;  free() is waiting
	  aio() has finished calling the user function;  free() continues execution
	*/
	fflk_lock(&f->lk);
	if (f->state == FI_CLOSED) {
		// user has closed the object
		fflk_unlock(&f->lk);
		fffileread_free(f);
		return;
	}
	FF_ASSERT(f->state == FI_ASYNC);
	f->state = FI_OK;
	if (f->nfy_user) {
		f->nfy_user = 0;
		f->conf.onread(f->conf.udata);
	}
	fflk_unlock(&f->lk);
}

/** Read using thread pool. */
static int fr_thpool_read(fffileread *f, ffstr *dst, uint64 off)
{
	if (off > f->eof) {
		errlog(f, "seek offset %U is bigger than file size %U", off, f->eof);
		return FFFILEREAD_RERR;
	} else if (off == f->eof)
		return FFFILEREAD_REOF;

	if (f->state == FI_ASYNC) {
		errlog(f, "async task is already pending", 0);
		return FFFILEREAD_RERR;
	}

	FF_ASSERT(f->wbuf != f->locked);
	struct buf *b = ffslice_itemT(&f->bufs, f->wbuf, struct buf);
	b->offset = ff_align_floor2(off, f->conf.bufalign);

	ffthpool_task *t;
	if (NULL == (t = ffthpool_task_new(sizeof(struct fr_task))))
		return FFFILEREAD_RERR;

	struct fr_task *ext = (void*)t->ext;
	ext->fd = f->fd;
	ffstr_set(&ext->buf, b->ptr, f->conf.bufsize);
	ext->off = b->offset;

	t->handler = &fr_aio;
	t->udata = f;
	f->state = FI_ASYNC;
	FF_ASSERT(f->iotask == NULL);
	f->iotask = t;
	f->nfy_user = 1;
	dbglog(f, "adding file read task to thread pool: offset:%xU", b->offset);
	if (0 != ffthpool_add(f->conf.thpool, t)) {
		f->iotask = NULL;
		syserrlog(f, "ffthpool_add", 0);
		ffthpool_task_free(t);
		return FFFILEREAD_RERR;
	}
	f->stat.nasync++;
	return FFFILEREAD_RASYNC;
}

/** Increment and reset to 0 on reaching the limit. */
#define ffint_cycleinc(n, lim)  (((n) + 1) % (lim))

/** Process the result of operation completed in thread pool's worker. */
static int fr_thpool_result(fffileread *f)
{
	int r = FFFILEREAD_RERR;
	ffthpool_task *t = f->iotask;
	struct fr_task *ext = (void*)t->ext;
	if (ext->result < 0) {
		fferr_set(ext->error);
		syserrlog(f, "%s", fffile_read_S);
		goto end;
	}

	dbglog(f, "buf#%u: read:%L offset:%xU"
		, f->wbuf, ext->result, ext->off);

	struct buf *b = ffslice_itemT(&f->bufs, f->wbuf, struct buf);
	FF_ASSERT(b->offset == ext->off);
	b->len = ext->result;
	f->wbuf = ffint_cycleinc(f->wbuf, f->conf.nbufs);
	f->stat.nread++;

	if ((uint)ext->result != f->conf.bufsize) {
		dbglog(f, "read the last block", 0);
		f->eof = b->offset + b->len;
	}
	r = 0;

end:
	ffthpool_task_free(f->iotask);
	f->iotask = NULL;
	return r;
}

int fffileread_getdata(fffileread *f, ffstr *dst, uint64 off, uint flags)
{
	int r, cachehit = 0;
	uint ibuf;
	struct buf *b;
	uint64 next;

	if (f->conf.thpool != NULL)
		flags &= ~(FFFILEREAD_FREADAHEAD | FFFILEREAD_FBACKWARD);

	if (f->iotask != NULL) {
		FF_ASSERT(f->state == FI_OK);
		if (0 != (r = fr_thpool_result(f)))
			return r;
		b = bufs_find(f, off);
		if (b != NULL)
			goto done;
		if (off == f->eof)
			return FFFILEREAD_REOF;
	}

	f->locked = (uint)-1;

	if (NULL != (b = bufs_find(f, off))) {
		if (f->async_off != off) {
			cachehit = 1;
			f->stat.ncached++;
		}
		f->async_off = (uint64)-1;
		goto done;
	}

	if (f->conf.thpool != NULL && !(flags & FFFILEREAD_FALLOWBLOCK))
		return fr_thpool_read(f, dst, off);

	if (f->state == FI_ASYNC) {
		f->nfy_user = 1;
		return FFFILEREAD_RASYNC;
	} else if (f->state == FI_EOF) {
		if (off > f->eof) {
			errlog(f, "seek offset %U is bigger than file size %U", off, f->eof);
			return FFFILEREAD_RERR;
		} else if (off == f->eof)
			return FFFILEREAD_REOF;
		f->state = FI_OK;
	}

	r = fr_read_off(f, ff_align_floor2(off, f->conf.bufalign));
	if (r == R_ASYNC) {
		f->async_off = off;
		f->nfy_user = 1;
		return FFFILEREAD_RASYNC;
	} else if (r == R_ERR)
		return FFFILEREAD_RERR;

	//R_READ or R_DONE
	b = bufs_find(f, off);
	if (r == R_DONE && b == NULL)
		return FFFILEREAD_REOF;
	FF_ASSERT(b != NULL);

done:
	ibuf = b - (struct buf*)f->bufs.ptr;
	f->locked = ibuf;

	next = b->offset + b->len;
	if (flags & FFFILEREAD_FBACKWARD)
		next = b->offset - f->conf.bufsize;
	if ((flags & FFFILEREAD_FREADAHEAD)
		&& (int64)next >= 0 && next != f->eof // don't read past eof
		&& f->conf.directio && f->conf.nbufs != 1) {

		if (NULL == bufs_find(f, next)
			&& f->state != FI_ASYNC) {
			if (f->wbuf == f->locked)
				f->wbuf = ffint_cycleinc(f->wbuf, f->conf.nbufs);
			fr_read_off(f, next);
		}
	}

	dbglog(f, "returning buf#%u  offset:%xU  cache-hit:%u"
		, ibuf, b->offset, cachehit);

	ffstr_set(dst, b->ptr, b->len);
	ffstr_shift(dst, off - b->offset);
	return FFFILEREAD_RREAD;
}

/** Async read has signalled.  Notify consumer about new events. */
static void fr_read_a(void *param)
{
	fffileread *f = param;
	int r;

	if (f->state == FI_CLOSED) {
		// object was closed while AIO is pending
		fffileread_free(f);
		return;
	}
	FF_ASSERT(f->state == FI_ASYNC);
	f->state = FI_OK;

	r = fr_read(f);
	if (r == R_ASYNC)
		return;

	if (f->nfy_user) {
		f->nfy_user = 0;
		f->conf.onread(f->conf.udata);
	}
}

/** Start reading at the specified aligned offset. */
static int fr_read_off(fffileread *f, uint64 off)
{
	struct buf *b = ffslice_itemT(&f->bufs, f->wbuf, struct buf);
	FF_ASSERT(f->wbuf != f->locked);
	buf_prepread(b, off);
	return fr_read(f);
}

/** Read from file. */
static int fr_read(fffileread *f)
{
	int r;
	struct buf *b;

	b = ffslice_itemT(&f->bufs, f->wbuf, struct buf);
	r = (int)ffaio_fread(&f->aio, b->ptr, f->conf.bufsize, b->offset, &fr_read_a);
	if (r < 0) {
		if (fferr_again(fferr_last())) {
			dbglog(f, "buf#%u: async read, offset:%Uk", f->wbuf, b->offset / 1024);
			f->state = FI_ASYNC;
			f->stat.nasync++;
			return R_ASYNC;
		}

		syserrlog(f, "%s: buf#%u offset:%Uk"
			, fffile_read_S, f->wbuf, b->offset / 1024);
		f->state = FI_ERR;
		return R_ERR;
	}

	b->len = r;
	f->stat.nread++;
	dbglog(f, "buf#%u: read:%L  offset:%Uk"
		, f->wbuf, b->len, b->offset / 1024);

	f->wbuf = ffint_cycleinc(f->wbuf, f->conf.nbufs);

	if ((uint)r != f->conf.bufsize) {
		dbglog(f, "read the last block", 0);
		f->eof = b->offset + b->len;
		f->state = FI_EOF;
		return R_DONE;
	}
	return R_DATA;
}

fffd fffileread_fd(fffileread *f)
{
	return f->fd;
}

void fffileread_stat(fffileread *f, struct fffileread_stat *st)
{
	*st = f->stat;
}
