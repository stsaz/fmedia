/**
Copyright (c) 2013 Simon Zolin
*/

#include "types.h"
#include <FFOS/time.h>
#include <FFOS/dir.h>
#include <FFOS/error.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>
#include <FFOS/socket.h>
#include "asyncio.h"
#include <FFOS/thread.h>
#include "atomic.h"

#include <time.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>


int _ffaio_result(ffaio_task *t)
{
	int r = -1;

	if (t->canceled) {
		t->canceled = 0;
		fferr_set(ECANCELED);
		goto done;
	}

	if (t->ev->events & EPOLLERR) {
		int er = 0;
		(void)ffskt_getopt(t->sk, SOL_SOCKET, SO_ERROR, &er);
		fferr_set(er);
		goto done;
	}

	r = 0;

done:
	t->ev = NULL;
	return r;
}


/*
Asynchronous file I/O in Linux:

1. Setup:
 aioctx = io_setup()
 eventfd handle = eventfd()
 ffkqu_attach(eventfd handle) -> kq

2. Add task:
 io_submit() + eventfd handle -> aioctx

3. Process event:
 ffkqu_wait(kq) --(eventfd handle)-> _ffaio_fctxhandler()

4. Execute task:
 io_getevents(aioctx) --(ffkevent*)-> ffkev_call()
*/

static FFINL int io_setup(unsigned nr_events, aio_context_t *ctx_idp)
{
	return syscall(SYS_io_setup, nr_events, ctx_idp);
}

static FFINL int io_destroy(aio_context_t ctx_id)
{
	return syscall(SYS_io_destroy, ctx_id);
}

static FFINL int io_submit(aio_context_t ctx_id, long nr, struct iocb **iocbpp)
{
	return syscall(SYS_io_submit, ctx_id, nr, iocbpp);
}

static FFINL int io_getevents(aio_context_t ctx_id, long min_nr, long nr, struct io_event *events, struct timespec *timeout)
{
	return syscall(SYS_io_getevents, ctx_id, min_nr, nr, events, timeout);
}

typedef struct _ffaio_filectx {
	ffkevent kev;
	aio_context_t aioctx;
	fffd kq;
} _ffaio_filectx;

#define FFAIO_FCTX_N  16 // max aio file contexts
enum { _FFAIO_NWORKERS = 256 };
struct _ffaio_filectx_m {
	struct _ffaio_filectx **items;
	uint n;
	fflock lk;
};
static struct _ffaio_filectx_m _ffaio_fctx;

static int _ffaio_ctx_init(struct _ffaio_filectx *fx, fffd kq);
static struct _ffaio_filectx* _ffaio_ctx_get(fffd kq);
static void _ffaio_ctx_close(struct _ffaio_filectx *fx);
static void _ffaio_fctxhandler(void *udata);

int ffaio_fctxinit(void)
{
	if (NULL == (_ffaio_fctx.items = ffmem_allocT(FFAIO_FCTX_N, struct _ffaio_filectx*)))
		return 1;
	fflk_init(&_ffaio_fctx.lk);
	return 0;
}

void ffaio_fctxclose(void)
{
	uint n = _ffaio_fctx.n;
	for (uint i = 0;  i != n;  i++) {
		_ffaio_ctx_close(_ffaio_fctx.items[i]);
		ffmem_free(_ffaio_fctx.items[i]);
	}
	ffmem_free0(_ffaio_fctx.items);
	_ffaio_fctx.n = 0;
}

/** eventfd has signaled.  Call handlers of completed file I/O requests. */
static void _ffaio_fctxhandler(void *udata)
{
	_ffaio_filectx *fx = (_ffaio_filectx*)udata;
	uint64 ev_n;
	ssize_t i, r;
	struct io_event evs[64];
	struct timespec ts = {0, 0};

	r = fffile_read(fx->kev.fd, &ev_n, sizeof(uint64));
	if (r != sizeof(uint64)) {
#ifdef FFDBG_AIO
		ffdbg_print(0, "%s(): read error from eventfd: %L, errno: %d\n", FF_FUNC, r, errno);
#endif
		return;
	}

	while (ev_n != 0) {

		r = io_getevents(fx->aioctx, 1, FFCNT(evs), evs, &ts);
		if (r <= 0) {
#ifdef FFDBG_AIO
			if (r < 0)
				ffdbg_print(0, "%s(): io_getevents() error: %d\n", FF_FUNC, errno);
#endif
			return;
		}

		for (i = 0;  i < r;  i++) {

			ffkqu_entry e = {0};
			ffkevent *kev = (void*)(size_t)(evs[i].data & ~1);
			ffaio_filetask *ft = FF_GETPTR(ffaio_filetask, kev, kev);
			ft->result = evs[i].res;
			//const struct iocb *cb = (void*)evs[i].obj;

			e.data.ptr = (void*)(size_t)evs[i].data;
			ffkev_call(&e);
		}

		ev_n -= r;
	}
}

/** Find (or create new) file AIO context for kqueue descriptor.
Thread-safe. */
static struct _ffaio_filectx* _ffaio_ctx_get(fffd kq)
{
	struct _ffaio_filectx *fx, *fxnew = NULL;
	uint n = _ffaio_fctx.n;

	for (uint i = 0;  ;  i++) {

		if (i == FFAIO_FCTX_N) {
			errno = EINVAL;
			fx = NULL;
			break;
		}

		if (i == n) {

			if (fxnew == NULL) {
				if (NULL == (fxnew = ffmem_new(struct _ffaio_filectx)))
					return NULL;
				if (0 != _ffaio_ctx_init(fxnew, kq)) {
					ffmem_free(fxnew);
					return NULL;
				}
			}

			// atomically write new context pointer and increase counter
			fflk_lock(&_ffaio_fctx.lk);
			n = FF_READONCE(_ffaio_fctx.n);
			if (i != n) {
				// another thread has occupied this slot
				fflk_unlock(&_ffaio_fctx.lk);
				continue;
			}
			_ffaio_fctx.items[i] = fxnew;
			ffcpu_fence_release();
			FF_WRITEONCE(_ffaio_fctx.n, i + 1);
			fflk_unlock(&_ffaio_fctx.lk);

			return fxnew;
		}

		ffcpu_fence_acquire();
		fx = _ffaio_fctx.items[i];
		if (fx->kq == kq)
			break;
	}

	if (fxnew != NULL) {
		_ffaio_ctx_close(fxnew);
		ffmem_free(fxnew);
	}

	return fx;
}

static int _ffaio_ctx_init(struct _ffaio_filectx *fx, fffd kq)
{
	fx->aioctx = 0;
	ffkev_init(&fx->kev);

	if (0 != io_setup(_FFAIO_NWORKERS, &fx->aioctx))
		goto err;

	fx->kev.fd = eventfd(0, EFD_NONBLOCK);
	if (fx->kev.fd == FF_BADFD) {
		ffaio_fctxclose();
		goto err;
	}
	if (0 != ffkqu_attach(kq, fx->kev.fd, ffkev_ptr(&fx->kev), FFKQU_ADD | FFKQU_READ))
		goto err;
	fx->kq = kq;

	fx->kev.oneshot = 0;
	fx->kev.handler = &_ffaio_fctxhandler;
	ffcpu_fence_release();
	fx->kev.udata = fx;
	return 0;

err:
	_ffaio_ctx_close(fx);
	return 1;
}

static void _ffaio_ctx_close(struct _ffaio_filectx *fx)
{
	if (fx->kev.fd != FF_BADFD)
		fffile_close(fx->kev.fd);
	ffkev_fin(&fx->kev);

	if (fx->aioctx != 0)
		io_destroy(fx->aioctx);
}

int ffaio_fattach(ffaio_filetask *ft, fffd kq, uint direct)
{
	if (!direct) {
		//don't use AIO
		ft->cb.aio_resfd = FF_BADFD;
		return 0;
	}

	struct _ffaio_filectx *fx = _ffaio_ctx_get(kq);
	if (fx == NULL)
		return 1;

	ft->fctx = fx;
	ft->cb.aio_resfd = 0;
	return 0;
}

static ssize_t _ffaio_fop(ffaio_filetask *ft, void *data, size_t len, uint64 off, ffaio_handler handler, uint op)
{
	struct iocb *cb = &ft->cb;

	if (ft->kev.pending) {
		ft->kev.pending = 0;

		if (ft->result < 0) {
			errno = -ft->result;
			return -1;
		}

		return ft->result;
	}

	struct _ffaio_filectx *fx = ft->fctx;

	ffmem_tzero(cb);
	cb->aio_data = (uint64)(size_t)ffkev_ptr(&ft->kev);
	cb->aio_lio_opcode = op;
	cb->aio_flags = IOCB_FLAG_RESFD;
	cb->aio_resfd = fx->kev.fd;

	cb->aio_fildes = ft->kev.fd;
	cb->aio_buf = (uint64)(size_t)data;
	cb->aio_nbytes = len;
	cb->aio_offset = off;

	if (1 != io_submit(fx->aioctx, 1, &cb)) {
		if (errno == ENOSYS || errno == EAGAIN)
			return -3; //no resources for this I/O operation or AIO isn't supported
		return -1;
	}

	ft->kev.pending = 1;
	ft->kev.handler = handler;
	errno = EAGAIN;
	return -1;
}

ssize_t ffaio_fwrite(ffaio_filetask *ft, const void *data, size_t len, uint64 off, ffaio_handler handler)
{
	ssize_t r = -3;
	if ((int)ft->cb.aio_resfd != FF_BADFD)
		r = _ffaio_fop(ft, (void*)data, len, off, handler, IOCB_CMD_PWRITE);
	if (r == -3)
		r = fffile_pwrite(ft->kev.fd, data, len, off);
	return r;
}

ssize_t ffaio_fread(ffaio_filetask *ft, void *data, size_t len, uint64 off, ffaio_handler handler)
{
	ssize_t r = -3;
	if ((int)ft->cb.aio_resfd != FF_BADFD)
		r = _ffaio_fop(ft, data, len, off, handler, IOCB_CMD_PREAD);
	if (r == -3)
		r = fffile_pread(ft->kev.fd, data, len, off);
	return r;
}
