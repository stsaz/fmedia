/**
Kernel queue.  kqueue, epoll, I/O completion ports.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/socket.h>

#define FFKQU_ADD  0
#define FFKQU_READ  FFKQ_READ
#define FFKQU_WRITE  FFKQ_WRITE

#define ffkqu_init()
static inline int ffkqu_close(ffkq kq)
{
	ffkq_close(kq);
	return 0;
}

#define ffkqu_entry  ffkq_event
#define ffkqu_data  ffkq_event_data
#define ffkqu_result  ffkq_event_flags
#define ffkqu_create  ffkq_create
#define ffkqu_attach  ffkq_attach
#define ffkqu_time  ffkq_time
#define ffkqu_settm  ffkq_time_set
static inline int ffkqu_wait(ffkq kq, ffkq_event *events, ffuint events_cap, ffkq_time *timeout)
{
	return ffkq_wait(kq, events, events_cap, *timeout);
}

#if defined FF_WIN
/**
Return the number of bytes transferred;  -1 on error. */
static FFINL int ffio_result(OVERLAPPED *ol) {
	DWORD t;
	BOOL b = GetOverlappedResult(NULL, ol, &t, 0);
	if (!b)
		return -1;
	return t;
}
#endif


typedef void (*ffkev_handler)(void *udata);

/** Connector between kernel events and user-space event handlers. */
typedef struct ffkevent {
	ffkev_handler handler;
	void *udata;
	union {
		fffd fd;
		ffskt sk;
	};
	unsigned side :1
		, oneshot :1 //"handler" is set to NULL each time the event signals
		, aiotask :1 //compatibility with ffaio_task
		, pending :1
		;

#if defined FF_WIN
	unsigned faio_direct :1; //use asynchronous file I/O
	OVERLAPPED ovl;
#endif
} ffkevent;

/** Initialize kevent. */
static FFINL void ffkev_init(ffkevent *kev)
{
	unsigned side = kev->side;
	memset(kev, 0, sizeof(ffkevent));
	kev->side = side;
	kev->oneshot = 1;
	kev->fd = FF_BADFD;
}

/** Finish working with kevent. */
static FFINL void ffkev_fin(ffkevent *kev)
{
	kev->side = !kev->side;
	kev->handler = NULL;
	kev->udata = NULL;
	kev->fd = FF_BADFD;
}

/** Get udata to register in the kernel queue. */
static FFINL void* ffkev_ptr(ffkevent *kev)
{
	return (void*)((size_t)kev | kev->side);
}

/** Attach kevent to kernel queue. */
#define ffkev_attach(kev, kq, flags) \
	ffkqu_attach(kq, (kev)->fd, ffkev_ptr(kev), FFKQU_ADD | (flags))

/** Call an event handler. */
FF_EXTN void ffkev_call(ffkqu_entry *e);

typedef ffkevent ffkevpost;

static void _ffev_handler(void *udata)
{
	ffkevpost *p = (ffkevpost*)udata;
	ffkq_post_consume(p->fd);
}

static inline int ffkqu_post_attach(ffkevpost *p, ffkq kq)
{
	ffkq_postevent post = ffkq_post_attach(kq, p);
	if (post == FFKQ_NULL)
		return -1;
	p->handler = _ffev_handler;
	p->udata = p;
	p->fd = post;
	return 0;
}
static inline void ffkqu_post_detach(ffkevpost *p, ffkq kq)
{
	ffkq_post_detach(p->fd, kq);
}
static inline int ffkqu_post(ffkevpost *p, void *data)
{
	return ffkq_post(p->fd, data);
}
