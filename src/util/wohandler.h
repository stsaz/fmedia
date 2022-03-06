/** Windows object handler - call a user function when a kernel object signals.
Copyright (c) 2015 Simon Zolin
*/

/*
       (HANDLE)
KERNEL -------> WOH-thread -> user_function()
*/

#pragma once

#include <FFOS/thread.h>
#include <FFOS/atomic.h>


typedef void (*ffwoh_handler_t)(void *udata);

struct ffwoh_item {
	ffwoh_handler_t handler;
	void *udata;
};

typedef struct ffwoh {
	uint count;
	HANDLE hdls[MAXIMUM_WAIT_OBJECTS]; //hdls[0] is wake_evt
	struct ffwoh_item items[MAXIMUM_WAIT_OBJECTS];

	HANDLE wake_evt
		, wait_evt;
	ffthd thd;
	uint tid;
	uint thderr;
	ffatomic cmd;
	fflock lk;
} ffwoh;

FF_EXTERN ffwoh* ffwoh_create(void);
FF_EXTERN void ffwoh_free(ffwoh *oh);

/** Associate HANDLE with user-function.
Can be called safely from a user-function.
Return 0 on success.  On failure, 'errno' may be set to an error from WOH-thread. */
FF_EXTERN int ffwoh_add(ffwoh *oh, HANDLE h, ffwoh_handler_t handler, void *udata);

/** Unregister HANDLE.
Can be called safely from a user-function. */
FF_EXTERN void ffwoh_rm(ffwoh *oh, HANDLE h);
