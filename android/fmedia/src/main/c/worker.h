/** fmedia/Android: Core: worker threads
2023, Simon Zolin */

#include <FFOS/thread.h>

struct wrk_ctx {
	ffvec workers; // struct worker[]
};

struct worker {
	ffthread th;
	uint64 tid;
	ffkq kq;
	ffkq_postevent kq_post;
	fftaskmgr taskmgr;
	uint quit;
};

static int FFTHREAD_PROCCALL wrk_loop(void *param)
{
	struct worker *w = param;
	w->tid = ffthread_curid();
	ffkq_time kqt;
	ffkq_time_set(&kqt, -1);
	dbglog0("%U: entering kqueue loop", w->tid);

	while (!FF_READONCE(w->quit)) {

		ffkq_event evs;
		uint n = ffkq_wait(w->kq, &evs, 1, kqt);

		if ((int)n < 0) {
			if (fferr_last() != EINTR) {
				syserrlog0("ffkq_wait");
				break;
			}
			continue;
		}

		ffkq_post_consume(w->kq_post);
		fftask_run(&w->taskmgr);
	}

	dbglog0("%U: leaving kqueue loop", w->tid);
	return 0;
}

static int wrk_create(struct worker *w)
{
	fftask_init(&w->taskmgr);
	if (FFKQ_NULL == (w->kq = ffkq_create())) {
		syserrlog0("ffkq_create");
		return -1;
	}
	if (FFKQ_NULL == (w->kq_post = ffkq_post_attach(w->kq, w))) {
		syserrlog0("ffkq_post_attach");
		return -1;
	}
	if (FFTHREAD_NULL == (w->th = ffthread_create(wrk_loop, w, 0))) {
		syserrlog0("ffthread_create");
		return -1;
	}
	return 0;
}

static void wrk_destroy(struct worker *w)
{
	if (w->th != FFTHREAD_NULL) {
		dbglog0("%U: destroy", w->tid);
		w->quit = 1;
		if (0 != ffkq_post(w->kq_post, w))
			syserrlog0("ffkq_post");
		ffthread_join(w->th, -1, NULL);
		dbglog0("thread exited");
		w->th = FFTHREAD_NULL;
		w->quit = 0;
	}
	ffkq_post_detach(w->kq_post, w->kq);
	w->kq_post = FFKQ_NULL;
	ffkq_close(w->kq);
	w->kq = FFKQ_NULL;
}


struct wrk_ctx* wrkx_init()
{
	struct wrk_ctx *wx = ffmem_new(struct wrk_ctx);
	struct worker *w = ffvec_zpushT(&wx->workers, struct worker);
	w->kq = FFKQ_NULL;
	w->kq_post = FFKQ_NULL;
	return wx;
}

void wrkx_destroy(struct wrk_ctx *wx)
{
	struct worker *w;
	FFSLICE_WALK(&wx->workers, w) {
		wrk_destroy(w);
	}
	ffvec_free(&wx->workers);
	ffmem_free(wx);
}

void wrkx_assign(struct wrk_ctx *wx, fmed_worker_task *wt)
{
	struct worker *w = ffslice_itemT(&wx->workers, 0, struct worker);
	if (w->th == FFTHREAD_NULL) {
		if (0 != wrk_create(w))
		{}
	}
	wt->wid = 0;
}

void wrkx_add(struct wrk_ctx *wx, fmed_worker_task *wt, void *func, void *param)
{
	wt->ts.handler = func;
	wt->ts.param = param;
	struct worker *w = ffslice_itemT(&wx->workers, wt->wid, struct worker);
	dbglog0("%U: task add: %p %p", w->tid, func, param);
	if (1 == fftask_post(&w->taskmgr, &wt->ts)) {
		if (0 != ffkq_post(w->kq_post, w))
			syserrlog0("ffkq_post");
	}
}

void wrkx_del(struct wrk_ctx *wx, fmed_worker_task *wt)
{
	struct worker *w = ffslice_itemT(&wx->workers, wt->wid, struct worker);
	dbglog0("%U: task del: %p %p", w->tid, wt->ts.handler, wt->ts.param);
	fftask_del(&w->taskmgr, &wt->ts);
}
