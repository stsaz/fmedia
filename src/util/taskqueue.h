/** User task queue.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include "list.h"
#include <util/ffos-compat/atomic.h>


typedef void (*fftask_handler)(void *param);

typedef struct fftask {
	fftask_handler handler;
	void *param;
	fflist_item sib;
} fftask;

#define fftask_set(tsk, func, udata) \
	(tsk)->handler = (func),  (tsk)->param = (udata)

/** Queue of arbitrary length containing tasks - user callback functions.
First in, first out.
One reader/deleter, multiple writers.
*/
typedef struct fftaskmgr {
	fflist tasks; //fftask[]
	fflock lk;
	uint max_run; //max. tasks to execute per fftask_run()
} fftaskmgr;

static inline void fftask_init(fftaskmgr *mgr)
{
	fflist_init(&mgr->tasks);
	fflk_init(&mgr->lk);
	mgr->max_run = 64;
}

/** Return TRUE if a task is in the queue. */
#define fftask_active(mgr, task)  ((task)->sib.next != NULL)

/** Add item into task queue.  Thread-safe.
Return 1 if the queue was empty. */
static inline uint fftask_post(fftaskmgr *mgr, fftask *task)
{
	uint r = 0;

	fflk_lock(&mgr->lk);
	if (fftask_active(mgr, task))
		goto done;
	r = fflist_empty(&mgr->tasks);
	fflist_ins(&mgr->tasks, &task->sib);
done:
	fflk_unlock(&mgr->lk);
	return r;
}

#define fftask_post4(mgr, task, func, _param) \
do { \
	(task)->handler = func; \
	(task)->param = _param; \
	fftask_post(mgr, task); \
} while (0)

/** Remove item from task queue. */
static inline void fftask_del(fftaskmgr *mgr, fftask *task)
{
	fflk_lock(&mgr->lk);
	if (!fftask_active(mgr, task))
		goto done;
	fflist_rm(&mgr->tasks, &task->sib);
done:
	fflk_unlock(&mgr->lk);
}

/** Call a handler for each task.
Return the number of tasks executed. */
static inline uint fftask_run(fftaskmgr *mgr)
{
	fflist_item *it, *sentl = fflist_sentl(&mgr->tasks);
	uint n, ntasks;

	for (n = mgr->max_run;  n != 0;  n--) {

		it = FF_READONCE(fflist_first(&mgr->tasks));
		if (it == sentl)
			break; //list is empty

		fflk_lock(&mgr->lk);
		FF_ASSERT(mgr->tasks.len != 0);
		_ffchain_link2(it->prev, it->next);
		it->next = NULL;
		ntasks = mgr->tasks.len--;
		fflk_unlock(&mgr->lk);

		fftask *task = FF_GETPTR(fftask, sib, it);

		(void)ntasks;
		FFDBG_PRINTLN(10, "[%L] %p handler=%p, param=%p"
			, ntasks, task, task->handler, task->param);

		task->handler(task->param);
	}

	return mgr->max_run - n;
}
