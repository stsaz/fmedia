/** Asynchronous IO.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/queue.h>
#include <FFOS/socket.h>


typedef void (*ffaio_handler)(void *udata);

/** The extended version of ffkevent: allows full duplex I/O and cancellation. */
typedef struct ffaio_task {
	ffaio_handler rhandler;
	void *udata;
	union {
		fffd fd;
		ffskt sk;
	};
	unsigned instance :1
		, oneshot :1
		, aiotask :1
		, rpending :1
		, wpending :1
		, udp :1 // Windows: set for UDP socket
		, canceled :1
		;

	ffaio_handler whandler;
	ffkqu_entry *ev;

#if defined FF_WIN
	byte sendbuf[1]; //allows transient buffer to be used for ffaio_send()
	OVERLAPPED rovl;
	OVERLAPPED wovl;
#endif
} ffaio_task;

/** Initialize AIO task. */
static FFINL void ffaio_init(ffaio_task *t) {
	unsigned inst = t->instance;
	ffmem_tzero(t);
	t->fd = FF_BADFD;
	t->instance = inst;
	t->aiotask = 1;
}

/** Finalize AIO task. */
static FFINL void ffaio_fin(ffaio_task *t) {
	unsigned inst = t->instance;
	t->fd = FF_BADFD;
	t->udata = NULL;
	t->rhandler = t->whandler = NULL;
	t->instance = !inst;
}

/** Get udata to register in the kernel queue. */
static FFINL void * ffaio_kqudata(ffaio_task *t) {
	return (void*)((size_t)t | t->instance);
}

/** Attach AIO task to kernel queue. */
#define ffaio_attach(task, kq, ev) \
	ffkqu_attach(kq, (task)->fd, ffaio_kqudata(task), FFKQU_ADD | (ev))

enum FFAIO_RET {
	FFAIO_ERROR = -1
	, FFAIO_ASYNC = -2
};

typedef struct ffaio_filetask ffaio_filetask;

#ifdef FF_UNIX

#include <FFOS/error.h>

#ifdef FF_LINUX
#include <linux/aio_abi.h>
#else
#include <aio.h>
#endif


#if defined FF_LINUX

/** Initialize file I/O context. */
FF_EXTN int ffaio_fctxinit(void);

/** Close all asynchronous file I/O contexts. */
FF_EXTN void ffaio_fctxclose(void);

struct ffaio_filetask {
	ffkevent kev;
	struct iocb cb;
	int result;
	void *fctx;
};

/** Attach ffaio_filetask to kqueue.
Linux: not thread-safe; only 1 kernel queue is supported for ALL AIO operations. */
FF_EXTN int ffaio_fattach(ffaio_filetask *ft, fffd kq, uint direct);

#elif defined FF_BSD

#define ffaio_fctxinit()  (0)
#define ffaio_fctxclose()

struct ffaio_filetask {
	ffkevent kev;
	struct aiocb acb;
};

static FFINL int ffaio_fattach(ffaio_filetask *ft, fffd kq, uint direct)
{
	(void)direct;
	ft->acb.aio_sigevent.sigev_notify_kqueue = kq;
	return 0;
}

#elif defined FF_APPLE

#define ffaio_fctxinit()  (0)
#define ffaio_fctxclose()

struct ffaio_filetask {
	ffkevent kev;
	struct aiocb acb;
};

static FFINL int ffaio_fattach(ffaio_filetask *ft, fffd kq, uint direct)
{
	(void)ft; (void)kq; (void)direct;
	return 0;
}

#endif


/** Get AIO task result.
If 'canceled' flag is set, reset it and return -1. */
FF_EXTN int _ffaio_result(ffaio_task *t);

typedef ffkevent ffaio_acceptor;

/** Initialize connection acceptor. */
static FFINL int ffaio_acceptinit(ffaio_acceptor *acc, fffd kq, ffskt lsk, void *udata, int family, int sktype)
{
	ffkev_init(acc);
	acc->sk = lsk;
	acc->udata = udata;
	(void)family;
	(void)sktype;
	return ffkqu_attach(kq, acc->sk, ffkev_ptr(acc), FFKQU_ADD | FFKQU_READ);
}

/** Close acceptor. */
#define ffaio_acceptfin(acc)

/** Get signaled events and flags of a kernel event structure. */
#define _ffaio_events(task, kqent)  ffkqu_result(kqent)

#else

#define ffaio_fctxinit()  (0)
#define ffaio_fctxclose()

struct ffaio_filetask {
	ffkevent kev;
};

#define ffaio_fattach(ft, kq, direct) \
	(((ft)->kev.faio_direct = direct) \
		? ffkqu_attach(kq, (ft)->kev.fd, ffkev_ptr(&(ft)->kev), 0) \
		: 0)


typedef struct ffaio_acceptor {
	ffkevent kev;
	int family;
	int sktype;

	ffskt csk;
	byte addrs[(FFADDR_MAXLEN + 16) * 2];
} ffaio_acceptor;

static FFINL int ffaio_acceptinit(ffaio_acceptor *acc, fffd kq, ffskt lsk, void *udata, int family, int sktype)
{
	ffkev_init(&acc->kev);
	acc->kev.sk = lsk;
	acc->kev.udata = udata;

	acc->family = family;
	acc->sktype = sktype;
	acc->csk = FF_BADSKT;
	ffmem_zero(acc->addrs, sizeof(acc->addrs));
	return ffkqu_attach(kq, (fffd)acc->kev.sk, ffkev_ptr(&acc->kev), 0);
}

static FFINL void ffaio_acceptfin(ffaio_acceptor *acc) {
	if (acc->csk != FF_BADSKT) {
		ffskt_close(acc->csk);
		acc->csk = FF_BADSKT;
	}
}

FF_EXTN int _ffaio_result(ffaio_task *t);

FF_EXTN int _ffaio_events(ffaio_task *t, const ffkqu_entry *e);

#endif

/** Async socket receive.
Windows: async operation is scheduled with an empty buffer.
Return bytes received or enum FFAIO_RET. */
FF_EXTN int ffaio_recv(ffaio_task *t, ffaio_handler handler, void *d, size_t cap);

/** Async socket send.
Windows: no more than 1 byte is sent using async operation.
Return bytes sent or enum FFAIO_RET. */
FF_EXTN int ffaio_send(ffaio_task *t, ffaio_handler handler, const void *d, size_t len);

FF_EXTN int ffaio_sendv(ffaio_task *t, ffaio_handler handler, ffiovec *iov, size_t iovcnt);

/** Connect to a remote server.
Return 0 on success or enum FFAIO_RET. */
FF_EXTN int ffaio_connect(ffaio_task *t, ffaio_handler handler, const struct sockaddr *addr, socklen_t addr_size);


/** Initialize file async I/O task. */
static FFINL void ffaio_finit(ffaio_filetask *ft, fffd fd, void *udata)
{
	ffkev_init(&ft->kev);
	ft->kev.fd = fd;
	ft->kev.udata = udata;
}

/** Asynchronous file I/O.
Return the number of bytes transferred or -1 on error.
Note: cancelling is not supported.
FreeBSD: kernel AIO must be enabled ("kldload aio"), or operations will block.
Linux, Windows: file must be opened with O_DIRECT, or operations will block.
Windows: writing into a new file on NTFS is always blocking. */
FF_EXTN ssize_t ffaio_fread(ffaio_filetask *ft, void *data, size_t len, uint64 off, ffaio_handler handler);
FF_EXTN ssize_t ffaio_fwrite(ffaio_filetask *ft, const void *data, size_t len, uint64 off, ffaio_handler handler);


/** Return TRUE if AIO task is active. */
#define ffaio_active(a)  ((a)->rhandler != NULL || (a)->whandler != NULL)
