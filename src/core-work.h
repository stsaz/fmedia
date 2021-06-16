/** fmedia: core: workers
2015,2021, Simon Zolin */

static void wrk_destroy(struct worker *w);
static int FFTHDCALL work_loop(void *param);

/** Initialize worker object */
static int wrk_init(struct worker *w, uint thread)
{
	fftask_init(&w->taskmgr);
	fftmrq_init(&w->tmrq);

	if (FF_BADFD == (w->kq = ffkqu_create())) {
		syserrlog("%s", ffkqu_create_S);
		return 1;
	}
	if (0 != ffkqu_post_attach(&w->kqpost, w->kq))
		syserrlog("%s", "ffkqu_post_attach");

	ffkev_init(&w->evposted);
	w->evposted.oneshot = 0;
	w->evposted.handler = &core_posted;

	if (thread) {
		w->thd = ffthd_create(&work_loop, w, 0);
		if (w->thd == FFTHD_INV) {
			syserrlog("%s", ffthd_create_S);
			wrk_destroy(w);
			return 1;
		}
		// w->id is set inside a new thread
	} else {
		w->id = ffthd_curid();
	}

	w->init = 1;
	return 0;
}

/** Destroy worker object */
static void wrk_destroy(struct worker *w)
{
	if (w->thd != FFTHD_INV) {
		ffthd_join(w->thd, -1, NULL);
		dbglog0("thread %xU exited", (int64)w->id);
		w->thd = FFTHD_INV;
	}
	fftmrq_destroy(&w->tmrq, w->kq);
	if (w->kq != FF_BADFD) {
		ffkqu_post_detach(&w->kqpost, w->kq);
		ffkqu_close(w->kq);
		w->kq = FF_BADFD;
	}
}

/** Find the worker with the least number of active jobs.
Initialize data and create a thread if necessary.
Return worker ID */
static uint work_assign(uint flags)
{
	struct worker *w, *ww = (void*)fmed->workers.ptr;
	uint id = 0, j = -1;

	if (flags == 0) {
		id = 0;
		w = &ww[0];
		goto done;
	}

	FFSLICE_WALK(&fmed->workers, w) {
		uint nj = ffatom_get(&w->njobs);
		if (nj < j) {
			id = w - ww;
			j = nj;
			if (nj == 0)
				break;
		}
	}
	w = &ww[id];

	if (!w->init
		&& 0 != wrk_init(w, 1)) {
		id = 0;
		w = &ww[0];
		goto done;
	}

done:
	if (flags & FMED_WORKER_FPARALLEL)
		ffatom_inc(&w->njobs);
	return id;
}

/** A job is completed */
static void work_release(uint wid, uint flags)
{
	struct worker *w = ffslice_itemT(&fmed->workers, wid, struct worker);
	if (flags & FMED_WORKER_FPARALLEL) {
		ssize_t n = ffatom_decret(&w->njobs);
		FMED_ASSERT(n >= 0);
	}
}

/** Get the number of available workers */
static uint work_avail()
{
	struct worker *w;
	FFSLICE_WALK(&fmed->workers, w) {
		if (ffatom_get(&w->njobs) == 0)
			return 1;
	}
	return 0;
}

void core_job_enter(uint id, size_t *ctx)
{
	struct worker *w = ffslice_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	*ctx = w->taskmgr.tasks.len;
}

ffbool core_job_shouldyield(uint id, size_t *ctx)
{
	struct worker *w = ffslice_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	return (*ctx != w->taskmgr.tasks.len);
}

ffbool core_ismainthr(void)
{
	struct worker *w = ffslice_itemT(&fmed->workers, 0, struct worker);
	return (w->id == ffthd_curid());
}

static int xtask(int signo, fftask *task, uint wid)
{
	struct worker *w = (void*)fmed->workers.ptr;
	FF_ASSERT(wid < fmed->workers.len);
	if (wid >= fmed->workers.len) {
		return -1;
	}
	w = &w[wid];

	dbglog0("task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, signo, fftask_active(&w->taskmgr, task), task->handler, task->param);

	if (w->kq == FFKQ_NULL) {

	} else if (signo == FMED_TASK_XPOST) {
		if (1 == fftask_post(&w->taskmgr, task))
			if (0 != ffkqu_post(&w->kqpost, &w->evposted))
				syserrlog("%s", "ffkqu_post");
	} else {
		fftask_del(&w->taskmgr, task);
	}
	return 0;
}

static void core_task(fftask *task, uint cmd)
{
	struct worker *w = (void*)fmed->workers.ptr;

	dbglog0("task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, cmd, fftask_active(&w->taskmgr, task), task->handler, task->param);

	if (w->kq == FFKQ_NULL) {
		return;
	}

	switch (cmd) {
	case FMED_TASK_POST:
		if (1 == fftask_post(&w->taskmgr, task))
			if (0 != ffkqu_post(&w->kqpost, &w->evposted))
				syserrlog("%s", "ffkqu_post");
		break;
	case FMED_TASK_DEL:
		fftask_del(&w->taskmgr, task);
		break;
	default:
		FF_ASSERT(0);
	}
}

static int core_timer(fftmrq_entry *tmr, int64 _interval, uint flags)
{
	struct worker *w = (void*)fmed->workers.ptr;
	int interval = _interval;
	uint period = ffmin((uint)ffabs(interval), TMR_INT);
	dbglog0("timer:%p  interval:%d  handler:%p  param:%p"
		, tmr, interval, tmr->handler, tmr->param);

	if (w->kq == FF_BADFD) {
		dbglog0("timer's not ready", 0);
		return -1;
	}

	if (fftmrq_active(&w->tmrq, tmr))
		fftmrq_rm(&w->tmrq, tmr);
	else if (interval == 0)
		return 0;

	if (interval == 0) {
		/* We should stop system timer only after some time of inactivity,
		 otherwise the next task may start the timer again.
		In this case we'd waste context switches.
		Anyway, we don't really need to stop it at all.

		if (fftmrq_empty(&w->tmrq)) {
			fftmrq_stop(&w->tmrq, w->kq);
			dbglog0("stopped kernel timer", 0);
		}
		*/
		return 0;
	}

	if (fftmrq_started(&w->tmrq) && period < w->period) {
		fftmrq_stop(&w->tmrq, w->kq);
		dbglog0("restarting kernel timer", 0);
	}

	if (!fftmrq_started(&w->tmrq)) {
		if (0 != fftmrq_start(&w->tmrq, w->kq, period)) {
			syserrlog("%s", "fftmrq_start()");
			return -1;
		}
		w->period = period;
		dbglog0("started kernel timer  interval:%u", period);
	}

	fftmrq_add(&w->tmrq, tmr, interval);
	return 0;
}

/** Worker's event loop */
static int FFTHDCALL work_loop(void *param)
{
	struct worker *w = param;
	w->id = ffthd_curid();
	ffkqu_entry *ents = ffmem_callocT(FMED_KQ_EVS, ffkqu_entry);
	if (ents == NULL)
		return -1;

	dbglog0("entering kqueue loop", 0);

	while (!FF_READONCE(fmed->stopped)) {

		uint nevents = ffkqu_wait(w->kq, ents, FMED_KQ_EVS, &fmed->kqutime);

		if ((int)nevents < 0) {
			if (fferr_last() != EINTR) {
				syserrlog("%s", ffkqu_wait_S);
				break;
			}
			continue;
		}

		for (uint i = 0;  i != nevents;  i++) {
			ffkqu_entry *ev = &ents[i];
			ffkev_call(ev);

			fftask_run(&w->taskmgr);
		}
	}

	ffmem_free(ents);
	return 0;
}
