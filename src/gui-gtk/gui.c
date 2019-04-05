/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>


const fmed_core *core;
ggui *gg;

typedef struct gtrk {
	void *trk;
	fmed_que_entry *qent;
	uint sample_rate;

	uint time_cur;
	uint time_total;
	int time_seek;
} gtrk;


//FMEDIA MODULE
static const void* gui_iface(const char *name);
static int gui_conf(const char *name, ffpars_ctx *ctx);
static int gui_sig(uint signo);
static void gui_destroy(void);
static const fmed_mod fmed_gui_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&gui_iface, &gui_sig, &gui_destroy, &gui_conf
};

//GUI-TRACK
static void* gtrk_open(fmed_filt *d);
static int gtrk_process(void *ctx, fmed_filt *d);
static void gtrk_close(void *ctx);
static const fmed_filter gui_track_iface = {
	&gtrk_open, &gtrk_process, &gtrk_close
};
static void gtrk_seek(uint cmd, uint val);

static FFTHDCALL int gui_worker(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_corecmd_handler(void *param);
static void corecmd_run(uint cmd, void *udata);

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_gui_mod;
}


static const void* gui_iface(const char *name)
{
	if (!ffsz_cmp(name, "gui"))
		return &gui_track_iface;
	return NULL;
}

static const ffpars_arg gui_conf_args[0] = {
};

static int gui_conf(const char *name, ffpars_ctx *ctx)
{
	if (ffsz_eq(name, "gui")) {
		ffpars_setargs(ctx, gg, gui_conf_args, FFCNT(gui_conf_args));
		return 0;
	}
	return -1;
}

static int gui_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		if (NULL == (gg = ffmem_new(ggui)))
			return -1;
		gg->conf.seek_step_delta = 5;
		gg->conf.seek_leap_delta = 60;
		return 0;

	case FMED_OPEN:
		if (NULL == (gg->qu = core->getmod("#queue.queue"))) {
			return 1;
		}
		gg->qu->cmd(FMED_QUE_SETONCHANGE, &gui_que_onchange);

		if (NULL == (gg->track = core->getmod("#core.track"))) {
			return 1;
		}

		fflk_setup();

		if (FFTHD_INV == (gg->th = ffthd_create(&gui_worker, gg, 0))) {
			return 1;
		}

		ffatom_waitchange(&gg->state, 0); //give the GUI thread some time to create controls
		return gg->load_err;

	case FMED_STOP:
		ffthd_join(gg->th, -1, NULL);
		break;
	}
	return 0;
}

static void gui_destroy(void)
{
	if (gg == NULL)
		return;

	ffmem_free0(gg);
}


static const ffui_ldr_ctl wmain_ctls[] = {
	FFUI_LDR_CTL(struct gui_wmain, wmain),
	FFUI_LDR_CTL(struct gui_wmain, mm),
	FFUI_LDR_CTL(struct gui_wmain, lpos),
	FFUI_LDR_CTL(struct gui_wmain, tpos),
	FFUI_LDR_CTL(struct gui_wmain, vlist),
	FFUI_LDR_CTL_END
};
static const ffui_ldr_ctl wabout_ctls[] = {
	FFUI_LDR_CTL(struct gui_wabout, wabout),
	FFUI_LDR_CTL(struct gui_wabout, labout),
	FFUI_LDR_CTL(struct gui_wabout, lurl),
	FFUI_LDR_CTL_END
};
static const ffui_ldr_ctl top_ctls[] = {
	FFUI_LDR_CTL(ggui, mfile),
	FFUI_LDR_CTL(ggui, mlist),
	FFUI_LDR_CTL(ggui, mplay),
	FFUI_LDR_CTL(ggui, mhelp),
	FFUI_LDR_CTL3(ggui, wmain, wmain_ctls),
	FFUI_LDR_CTL3(ggui, wabout, wabout_ctls),
	FFUI_LDR_CTL_END
};

static void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *g = udata;
	return ffui_ldr_findctl(top_ctls, g, name);
}

static const char *const action_str[] = {
	"A_QUIT",

	"A_PLAY",
	"A_SEEK",
	"A_STOP",
	"A_STOP_AFTER",
	"A_NEXT",
	"A_PREV",
	"A_FFWD",
	"A_RWND",
	"A_LEAP_FWD",
	"A_LEAP_BACK",

	"A_SELECTALL",
	"A_CLEAR",

	"A_ABOUT",
};

static int gui_getcmd(void *udata, const ffstr *name)
{
	(void)udata;
	for (uint i = 0;  i != FFCNT(action_str);  i++) {
		if (ffstr_eqz(name, action_str[i]))
			return i + 1;
	}
	return 0;
}

static int load_ui()
{
	int r = -1;
	char *fn;
	ffui_loader ldr;
	ffui_ldr_init2(&ldr, &gui_getctl, &gui_getcmd, gg);

	if (NULL == (fn = core->getpath(FFSTR("./fmedia.gui"))))
		goto done;

	if (0 != ffui_ldr_loadfile(&ldr, fn)) {
		char *msg = ffsz_alfmt("parsing fmedia.gui: %s", ffui_ldr_errstr(&ldr));
		errlog("%s", msg);
		ffmem_free(msg);
		goto done;
	}
	r = 0;

done:
	ffui_ldr_fin(&ldr);
	ffmem_free0(fn);
	return r;
}

static FFTHDCALL int gui_worker(void *param)
{
	ffui_init();
	ffui_wnd_initstyle();

	if (0 != load_ui())
		goto err;
	wmain_init();
	wabout_init();

	FF_WRITEONCE(gg->state, 1);
	dbglog("entering UI loop");
	ffui_run();
	dbglog("exited UI loop");

	corecmd_add(A_ONCLOSE, NULL);
	goto done;

err:
	FF_WRITEONCE(gg->state, 1);
	gg->load_err = 1;
done:
	ffui_uninit();
	return 0;
}


struct corecmd {
	fftask tsk;
	uint cmd;
	void *udata;
};

static void gui_corecmd_handler(void *param)
{
	struct corecmd *c = param;
	corecmd_run(c->cmd, c->udata);
	ffmem_free(c);
}

void corecmd_add(uint cmd, void *udata)
{
	dbglog("%s cmd:%u  udata:%p", __func__, cmd, udata);
	struct corecmd *c = ffmem_new(struct corecmd);
	if (c == NULL)
		return;
	c->tsk.handler = &gui_corecmd_handler;
	c->tsk.param = c;
	c->cmd = cmd;
	c->udata = udata;
	core->task(&c->tsk, FMED_TASK_POST);
}

static void corecmd_run(uint cmd, void *udata)
{
	dbglog("%s cmd:%u  udata:%p", __func__, cmd, udata);

	switch ((enum ACTION)cmd) {
	case A_PLAY:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PLAY, (void*)gg->qu->fmed_queue_item(-1, gg->focused));
		break;

	case A_STOP:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		break;

	case A_STOP_AFTER:
		gg->qu->cmd(FMED_QUE_STOP_AFTER, NULL);
		break;

	case A_NEXT:
	case A_PREV: {
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		uint id = (cmd == A_NEXT) ? FMED_QUE_NEXT2 : FMED_QUE_PREV2;
		gg->qu->cmd(id, (gg->curtrk != NULL) ? gg->curtrk->qent : NULL);
		break;
	}

	case A_SEEK:
	case A_FFWD:
	case A_RWND:
	case A_LEAP_FWD:
	case A_LEAP_BACK:
		gtrk_seek(cmd, (size_t)udata);
		break;

	case A_CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR | FMED_QUE_NO_ONCHANGE, NULL);
		ffui_post_view_clear(&gg->wmain.vlist);
		break;

	case A_ONDROPFILE: {
		ffstr *d = udata;
		ffstr s = *d, ln;
		while (s.len != 0) {
			ffstr_nextval3(&s, &ln, '\n');
			if (!ffs_matchz(ln.ptr, ln.len, "file://"))
				continue;
			ffstr_shift(&ln, FFSLEN("file://"));
			wmain_ent_add(&ln);
		}
		ffstr_free(d);
		ffmem_free(d);
		break;
	}

	case A_ONCLOSE:
		core->sig(FMED_STOP);
		break;

	default:
		FF_ASSERT(0);
	}
}


static void gui_que_onchange(fmed_que_entry *ent, uint flags)
{
	uint idx;

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD:
		if (flags & FMED_QUE_ADD_DONE)
			break;
		//fallthrough
	case FMED_QUE_ONRM:
		if (!gg->qu->cmdv(FMED_QUE_ISCURLIST, ent))
			return;
		break;
	}

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD:
		idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		ffui_thd_post(&wmain_ent_added, (void*)(size_t)idx, FFUI_POST_WAIT);
		break;

	case FMED_QUE_ONRM:
		idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		wmain_ent_removed(idx);
		break;

	case FMED_QUE_ONCLEAR:
		break;
	}
}

static void* gtrk_open(fmed_filt *d)
{
	fmed_que_entry *ent = (void*)d->track->getval(d->trk, "queue_item");
	if (ent == FMED_PNULL)
		return FMED_FILT_SKIP;

	gtrk *t = ffmem_new(gtrk);
	if (t == NULL)
		return NULL;
	t->time_seek = -1;
	t->qent = ent;

	if (d->audio.fmt.format == 0) {
		errlog("audio format isn't set");
		ffmem_free(t);
		return NULL;
	}

	t->sample_rate = d->audio.fmt.sample_rate;
	t->time_total = ffpcm_time(d->audio.total, t->sample_rate) / 1000;
	wmain_newtrack(ent, t->time_total);

	t->trk = d->trk;
	gg->curtrk = t;
	return t;
}

static int gtrk_process(void *ctx, fmed_filt *d)
{
	gtrk *t = ctx;

	if ((int64)d->audio.pos == FMED_NULL)
		goto done;

	if (t->time_seek != -1) {
		d->audio.seek = t->time_seek * 1000;
		d->snd_output_clear = 1;
		t->time_seek = -1;
		return FMED_RMORE;
	}

	uint playtime = (uint)(ffpcm_time(d->audio.pos, t->sample_rate) / 1000);
	if (playtime == t->time_cur)
		goto done;
	t->time_cur = playtime;
	wmain_update(playtime, t->time_total);

done:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}

static void gtrk_close(void *ctx)
{
	gtrk *t = ctx;
	if (gg->curtrk == t) {
		gg->curtrk = NULL;
		wmain_fintrack();
	}
}

static void gtrk_seek(uint cmd, uint val)
{
	gtrk *t = gg->curtrk;
	int delta;
	if (t == NULL)
		return;

	switch (cmd) {
	case A_SEEK:
		t->time_seek = val;
		break;

	case A_FFWD:
		delta = gg->conf.seek_step_delta;
		goto use_delta;
	case A_RWND:
		delta = -(int)gg->conf.seek_step_delta;
		goto use_delta;

	case A_LEAP_FWD:
		delta = gg->conf.seek_leap_delta;
		goto use_delta;
	case A_LEAP_BACK:
		delta = -(int)gg->conf.seek_leap_delta;
		goto use_delta;

use_delta:
		t->time_seek = ffmax((int)t->time_cur + delta, 0);
		break;
	}
}
