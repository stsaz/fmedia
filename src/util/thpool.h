/** Thread pool for offloading the operations that may take a long time to complete.
Copyright (c) 2020 Simon Zolin
*/

#pragma once

#include <FFOS/atomic.h>


/** Configuration. */
typedef struct ffthpoolconf {
	uint maxthreads; // max. allowed threads
	uint maxqueue; // task queue capacity
} ffthpoolconf;

typedef struct ffthpool ffthpool;

/** Create thread pool. */
FF_EXTERN ffthpool* ffthpool_create(ffthpoolconf *conf);

/** Free thread pool object. */
FF_EXTERN int ffthpool_free(ffthpool *p);

typedef struct ffthpool_task ffthpool_task;
typedef void (*ffthpool_handler)(ffthpool_task *t);

/** Shared data for a task object.
Extended data area contains user-defined data. */
struct ffthpool_task {
	ffatomic32 ref;
	ffthpool_handler handler;
	void *udata;

	byte ext[0];
};

/** Create new task. */
FF_EXTERN ffthpool_task* ffthpool_task_new(uint addsize);

/** Free task object. */
FF_EXTERN void ffthpool_task_free(ffthpool_task *t);

/** Add task to the queue.  Thread-safe.
Create additional threads when necessary. */
FF_EXTERN int ffthpool_add(ffthpool *p, ffthpool_task *task);
