/**
Copyright (c) 2020 Simon Zolin
*/

#include "thpool.h"
#include "ring.h"
#include <FFOS/thread.h>
#include <FFOS/semaphore.h>
#include <FFOS/error.h>
#include <ffbase/slice.h>


struct ffthpool {
	ffthpoolconf conf;
	ffslice threads; // ffthd[]
	ffring queue; // ffthpool_task*[]
	fflock lk;
	uint stop;
	ffsem sem;
};

ffthpool* ffthpool_create(ffthpoolconf *conf)
{
	if (conf->maxthreads == 0
		|| conf->maxqueue == 0) {
		fferr_set(EINVAL);
		return NULL;
	}

	ffthpool *p;
	if (NULL == (p = ffmem_new(ffthpool)))
		return NULL;
	p->conf = *conf;

	if (FFSEM_INV == (p->sem = ffsem_open(NULL, 0, 0)))
		goto end;

	if (NULL == ffslice_allocT(&p->threads, p->conf.maxthreads, ffthd))
		goto end;

	if (0 != ffring_create(&p->queue, p->conf.maxqueue, 64))
		goto end;

	return p;

end:
	ffthpool_free(p);
	return NULL;
}

int ffthpool_free(ffthpool *p)
{
	if (p == NULL)
		return 0;

	int rc = 0;
	FF_WRITEONCE(p->stop, 1);

	ffthd *th;
	FFSLICE_WALK_T(&p->threads, th, ffthd) {
		ffsem_post(p->sem);
	}
	FFSLICE_WALK_T(&p->threads, th, ffthd) {
		if (*th != FFTHD_INV && 0 != ffthd_join(*th, 5000, NULL))
			rc = -1;
		else
			*th = FFTHD_INV;
	}
	if (rc == 0) {
		ffslice_free(&p->threads);
		ffring_destroy(&p->queue);
		ffsem_close(p->sem);
		ffmem_free(p);
	}
	return rc;
}

static int FFTHDCALL ffthpool_loop(void *udata)
{
	ffthpool *p = udata;

	while (!FF_READONCE(p->stop)) {

		void *ptr;
		if (0 != ffring_read(&p->queue, &ptr)) {
			ffsem_wait(p->sem, -1);
			continue;
		}

		ffthpool_task *t = ptr;
		t->handler(t);
		ffthpool_task_free(t);
	}
	return 0;
}

/** Add a new thread. */
static int tp_newthread(ffthpool *p)
{
	int r = -1;
	fflk_lock(&p->lk);
	if (p->threads.len == p->conf.maxthreads) {
		r = 0;
		goto end;
	}

	ffthd th;
	if (FFTHD_INV == (th = ffthd_create(&ffthpool_loop, p, 0)))
		goto end;
	*ffslice_pushT(&p->threads, p->conf.maxthreads, ffthd) = th;
	r = 0;

end:
	fflk_unlock(&p->lk);
	return r;
}

int ffthpool_add(ffthpool *p, ffthpool_task *task)
{
	ffbool empty = ffring_empty(&p->queue);

	ffatom32_inc(&task->ref);
	if (0 != ffring_write(&p->queue, task)) {
		ffatom32_dec(&task->ref);
		fferr_set(EOVERFLOW);
		return -1;
	}

	if ((!empty || p->threads.len == 0)
		&& p->threads.len < p->conf.maxthreads) {
		if (0 != tp_newthread(p))
			return -1;
	}

	ffsem_post(p->sem);
	return 0;
}

ffthpool_task* ffthpool_task_new(uint addsize)
{
	ffthpool_task *t;
	if (NULL == (t = ffmem_alloc(sizeof(ffthpool_task) + addsize)))
		return NULL;
	ffatom_set(&t->ref, 1);
	t->handler = NULL;
	t->udata = NULL;
	return t;
}

void ffthpool_task_free(ffthpool_task *t)
{
	if (t == NULL)
		return;
	if (ffatom32_decret(&t->ref) == 0)
		ffmem_free(t);
}
