/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/path.h>
#include <FFOS/dir.h>
#include <FFOS/process.h>


const fmed_core *core;
ggui *gg;

typedef struct gtrk {
	void *trk;
	fmed_que_entry *qent;
	fmed_filt *d;
	uint sample_rate;

	uint time_cur;
	uint time_total;
	int time_seek;

	uint paused :1;
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
static void gtrk_vol(uint pos);

static FFTHDCALL int gui_worker(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_corecmd_handler(void *param);
static void corecmd_run(uint cmd, void *udata);
static void file_showpcm(void);
static void file_del(void);
static void lists_load(void);
static void lists_save(void);
static void list_add(const ffstr *fn);
static void list_rmitems(void);
static void showdir_selected(void);
static void showdir(const char *fn);

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
		gg->conf.autosave_playlists = 1;
		gg->vol = 100;
		gg->go_pos = -1;
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
	FFUI_LDR_CTL(struct gui_wmain, bpause),
	FFUI_LDR_CTL(struct gui_wmain, bstop),
	FFUI_LDR_CTL(struct gui_wmain, bprev),
	FFUI_LDR_CTL(struct gui_wmain, bnext),
	FFUI_LDR_CTL(struct gui_wmain, lpos),
	FFUI_LDR_CTL(struct gui_wmain, tvol),
	FFUI_LDR_CTL(struct gui_wmain, tpos),
	FFUI_LDR_CTL(struct gui_wmain, vlist),
	FFUI_LDR_CTL(struct gui_wmain, stbar),
	FFUI_LDR_CTL(struct gui_wmain, tray_icon),
	FFUI_LDR_CTL_END
};
static const ffui_ldr_ctl wabout_ctls[] = {
	FFUI_LDR_CTL(struct gui_wabout, wabout),
	FFUI_LDR_CTL(struct gui_wabout, labout),
	FFUI_LDR_CTL(struct gui_wabout, lurl),
	FFUI_LDR_CTL_END
};
static const ffui_ldr_ctl wuri_ctls[] = {
	FFUI_LDR_CTL(struct gui_wuri, wuri),
	FFUI_LDR_CTL(struct gui_wuri, turi),
	FFUI_LDR_CTL(struct gui_wuri, bok),
	FFUI_LDR_CTL_END
};
static const ffui_ldr_ctl top_ctls[] = {
	FFUI_LDR_CTL(ggui, mfile),
	FFUI_LDR_CTL(ggui, mlist),
	FFUI_LDR_CTL(ggui, mplay),
	FFUI_LDR_CTL(ggui, mhelp),
	FFUI_LDR_CTL(ggui, dlg),
	FFUI_LDR_CTL3(ggui, wmain, wmain_ctls),
	FFUI_LDR_CTL3(ggui, wabout, wabout_ctls),
	FFUI_LDR_CTL3(ggui, wuri, wuri_ctls),
	FFUI_LDR_CTL_END
};

static void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *g = udata;
	return ffui_ldr_findctl(top_ctls, g, name);
}

static const char *const action_str[] = {
	"A_LIST_ADDFILE",
	"A_LIST_ADDURL",
	"A_SHOWPCM",
	"A_SHOWDIR",
	"A_DELFILE",
	"A_SHOW",
	"A_HIDE",
	"A_QUIT",

	"A_PLAY",
	"A_PLAYPAUSE",
	"A_SEEK",
	"A_STOP",
	"A_STOP_AFTER",
	"A_NEXT",
	"A_PREV",
	"A_FFWD",
	"A_RWND",
	"A_LEAP_FWD",
	"A_LEAP_BACK",
	"A_SETGOPOS",
	"A_GOPOS",
	"A_VOL",
	"A_VOLUP",
	"A_VOLDOWN",
	"A_VOLRESET",

	"A_LIST_SAVE",
	"A_LIST_SELECTALL",
	"A_LIST_REMOVE",
	"A_LIST_RMDEAD",
	"A_LIST_CLEAR",
	"A_LIST_RANDOM",

	"A_ABOUT",
	"A_CONF_EDIT",
	"A_FMEDGUI_EDIT",
	"A_README_SHOW",
	"A_CHANGES_SHOW",

	"A_URL_ADD",
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
	char *fn, *fnconf = NULL;
	ffui_loader ldr;
	ffui_ldr_init2(&ldr, &gui_getctl, &gui_getcmd, gg);

	if (NULL == (fn = core->getpath(FFSTR("./fmedia.gui"))))
		goto done;
	if (NULL == (fnconf = ffsz_alfmt("%s%s", core->props->user_path, CTL_CONF_FN)))
		goto done;

	if (0 != ffui_ldr_loadfile(&ldr, fn)) {
		char *msg = ffsz_alfmt("parsing fmedia.gui: %s", ffui_ldr_errstr(&ldr));
		errlog("%s", msg);
		ffmem_free(msg);
		goto done;
	}

	ffui_ldr_loadconf(&ldr, fnconf);
	r = 0;

done:
	ffui_ldr_fin(&ldr);
	ffmem_free(fn);
	ffmem_free(fnconf);
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
	wuri_init();

	if (gg->conf.autosave_playlists)
		corecmd_add(LOADLISTS, NULL);

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

	case A_SHOWPCM:
		file_showpcm();
		break;

	case A_SHOWDIR:
		showdir_selected();
		break;

	case A_DELFILE:
		file_del();
		break;

	case A_PLAY:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PLAY, (void*)gg->qu->fmed_queue_item(-1, gg->focused));
		break;

	case A_PLAYPAUSE:
		if (gg->curtrk == NULL) {
			gg->qu->cmd(FMED_QUE_PLAY, NULL);
			break;
		}

		if (gg->curtrk->paused) {
			gg->curtrk->paused = 0;
			wmain_status("");
			gg->curtrk->d->snd_output_pause = 0;
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_UNPAUSE);
			break;
		}

		gg->curtrk->d->snd_output_pause = 1;
		wmain_status("Paused");
		gg->curtrk->paused = 1;
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
	case A_SETGOPOS:
		if (gg->curtrk != NULL) {
			gg->go_pos = gg->curtrk->time_cur;
			wmain_status("Marker: %u:%02u"
				, gg->go_pos / 60, gg->go_pos % 60);
		}
		break;
	case A_GOPOS:
		if (gg->curtrk != NULL && gg->go_pos != (uint)-1)
			gg->curtrk->time_seek = gg->go_pos;
		break;

	case A_VOL:
		gg->vol = (size_t)udata;
		gtrk_vol(gg->vol);
		break;
	case A_VOLUP:
		gg->vol = ffmin(gg->vol + 5, MAXVOL);
		gtrk_vol(gg->vol);
		break;
	case A_VOLDOWN:
		gg->vol = ffmax((int)gg->vol - 5, 0);
		gtrk_vol(gg->vol);
		break;
	case A_VOLRESET:
		gg->vol = 100;
		gtrk_vol(gg->vol);
		break;

	case A_LIST_SAVE: {
		char *list_fn = udata;
		gg->qu->fmed_queue_save(-1, list_fn);
		ffmem_free(list_fn);
		break;
	}
	case A_LIST_REMOVE:
		list_rmitems();
		break;

	case A_LIST_RMDEAD:
		gg->qu->cmd(FMED_QUE_RMDEAD, NULL);
		break;

	case A_LIST_CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR | FMED_QUE_NO_ONCHANGE, NULL);
		wmain_list_clear();
		break;

	case A_LIST_RANDOM:
		core->props->list_random = !core->props->list_random;
		break;

	case A_ONDROPFILE: {
		ffstr *d = udata;
		ffstr s = *d, ln;
		while (s.len != 0) {
			ffstr_nextval3(&s, &ln, '\n');
			if (!ffs_matchz(ln.ptr, ln.len, "file://"))
				continue;
			ffstr_shift(&ln, FFSLEN("file://"));
			list_add(&ln);
		}
		ffstr_free(d);
		ffmem_free(d);
		break;
	}

	case A_URL_ADD: {
		ffstr *s = udata;
		list_add(s);
		ffstr_free(s);
		ffmem_free(s);
		break;
	}

	case A_ONCLOSE:
		if (gg->conf.autosave_playlists)
			lists_save();
		core->sig(FMED_STOP);
		break;

	case LOADLISTS:
		lists_load();
		break;

	default:
		FF_ASSERT(0);
	}
}

/** Execute GUI text editor to open a file. */
void gui_showtextfile(uint id)
{
	const char *name = NULL;
	char *fn;
	switch (id) {
	case A_CONF_EDIT:
		name = FMED_GLOBCONF; break;

	case A_FMEDGUI_EDIT:
		name = "fmedia.gui"; break;

	case A_README_SHOW:
		name = "README.txt"; break;

	case A_CHANGES_SHOW:
		name = "CHANGES.txt"; break;
	default:
		return;
	}

	if (NULL == (fn = core->getpath(name, ffsz_len(name))))
		return;

	const char *argv[] = {
		"xdg-open", fn, NULL
	};
	ffps ps = ffps_exec("/usr/bin/xdg-open", argv, (const char**)environ);
	if (ps == FFPS_INV) {
		syserrlog("ffps_exec", 0);
		goto done;
	}

	dbglog("spawned editor: %u", (int)ffps_id(ps));
	ffps_close(ps);

done:
	ffmem_free(fn);
}

static void showdir_selected(void)
{
	int i;
	ffarr4 *sel = (void*)ffui_send_view_getsel(&gg->wmain.vlist);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, sel))) {
		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);
		showdir(ent->url.ptr);
		break;
	}
	ffui_view_sel_free(sel);
}

/** Execute GUI file manager to show the directory with a file. */
static void showdir(const char *fn)
{
	char *dirz;
	ffstr dir;
	ffpath_split2(fn, ffsz_len(fn), &dir, NULL);

	if (NULL == (dirz = ffsz_alcopystr(&dir)))
		return;

	const char *argv[] = {
		"xdg-open", dirz, NULL
	};
	ffps ps = ffps_exec("/usr/bin/xdg-open", argv, (const char**)environ);
	if (ps == FFPS_INV) {
		syserrlog("ffps_exec", 0);
		goto done;
	}

	dbglog("spawned file manager: %u", (int)ffps_id(ps));
	ffps_close(ps);

done:
	ffmem_free(dirz);
}

/** Save playlists to disk. */
static void lists_save(void)
{
	ffarr buf = {};
	const char *fn = core->props->user_path;

	if (NULL == ffarr_alloc(&buf, ffsz_len(fn) + FFSLEN(AUTOPLIST_FN) + FFINT_MAXCHARS + 1))
		goto end;

	uint n = 1;
	for (uint i = 0; ; i++) {
		buf.len = 0;
		ffstr_catfmt(&buf, "%s" AUTOPLIST_FN "%Z", fn, n++);
		gg->qu->fmed_queue_save(i, buf.ptr);
		break;
	}

end:
	ffarr_free(&buf);
}

/** Load playlists saved in the previous session. */
static void lists_load(void)
{
	const char *fn = core->props->user_path;
	ffarr buf = {};

	if (NULL == ffarr_alloc(&buf, ffsz_len(fn) + FFSLEN(AUTOPLIST_FN) + FFINT_MAXCHARS + 1))
		goto end;

	for (uint i = 1;  ;  i++) {
		buf.len = 0;
		ffstr_catfmt(&buf, "%s" AUTOPLIST_FN "%Z", fn, i);
		buf.len--;
		if (!fffile_exists(buf.ptr))
			break;
		list_add((ffstr*)&buf);
	}

end:
	ffarr_free(&buf);
	return;
}

static void list_add(const ffstr *fn)
{
	int t = FMED_FT_FILE;
	if (!ffs_matchz(fn->ptr, fn->len, "http://")) {
		char *fnz = ffsz_alcopystr(fn);
		t = core->cmd(FMED_FILETYPE, fnz);
		ffmem_free(fnz);
		if (t == FMED_FT_UKN)
			return;
	}

	fmed_que_entry e, *pe;
	ffmem_tzero(&e);
	e.url = *fn;
	if (NULL == (pe = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, -1, &e)))
		return;
	uint idx = gg->qu->cmdv(FMED_QUE_ID, pe);
	wmain_ent_added(idx);

	if (t == FMED_FT_DIR || t == FMED_FT_PLIST)
		gg->qu->cmd2(FMED_QUE_EXPAND, pe, 0);
}


/** Remove items from list. */
static void list_rmitems(void)
{
	int i, n = 0;
	ffarr4 *sel = (void*)ffui_send_view_getsel(&gg->wmain.vlist);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, sel))) {
		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i - n);
		gg->qu->cmd2(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ent, 0);
		wmain_ent_removed(i - n);
		n++;
	}
	ffui_view_sel_free(sel);
	wmain_status("Removed %u items", n);
}

/** For each selected item in list start a new track to analyze PCM peaks. */
static void file_showpcm(void)
{
	int i;
	fmed_que_entry *ent;
	void *trk;

	ffarr4 *sel = (void*)ffui_send_view_getsel(&gg->wmain.vlist);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, sel))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

		if (NULL == (trk = gg->track->create(FMED_TRK_TYPE_PLAYBACK, ent->url.ptr)))
			break;

		fmed_trk *trkconf = gg->track->conf(trk);
		trkconf->pcm_peaks = 1;
		gg->track->cmd(trk, FMED_TRACK_XSTART);
	}
	ffui_view_sel_free(sel);
}

/** Delete all selected files. */
static void file_del(void)
{
	int i, n = 0;
	fmed_que_entry *ent;

	ffarr4 *sel = (void*)ffui_send_view_getsel(&gg->wmain.vlist);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, sel))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i - n);

		char *fn = ffsz_alfmt("%S.deleted", &ent->url);
		if (fn == NULL)
			break;

		if (0 == fffile_rename(ent->url.ptr, fn)) {
			gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ent);
			wmain_ent_removed(i - n);
			n++;
		} else
			syserrlog("can't rename file: %s", fn);

		ffmem_free(fn);
	}
	ffui_view_sel_free(sel);
	wmain_status("Deleted %u files", n);
}

static const char *const ctl_setts[] = {
	"wmain.position",
};

/** Write graphical controls' settings to a file. */
void ctlconf_write(void)
{
	char *fn;
	ffui_loaderw ldr = {};

	if (NULL == (fn = ffsz_alfmt("%s%s", core->props->user_path, CTL_CONF_FN)))
		return;

	ldr.getctl = &gui_getctl;
	ldr.udata = gg;
	ffui_ldr_setv(&ldr, ctl_setts, FFCNT(ctl_setts), 0);

	if (0 != ffui_ldr_write(&ldr, fn) && fferr_nofile(fferr_last())) {
		if (0 != ffdir_make_path(fn, 0) && fferr_last() != EEXIST) {
			syserrlog("Can't create directory for the file: %s", fn);
			goto done;
		}
		if (0 != ffui_ldr_write(&ldr, fn))
			syserrlog("Can't write configuration file: %s", fn);
	}

done:
	ffmem_free(fn);
	ffui_ldrw_fin(&ldr);
}


/** Process events from queue module. */
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
		if (flags & FMED_QUE_ADD_DONE)
			break;
		idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		wmain_ent_added(idx);
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
	t->time_cur = -1;
	t->time_seek = -1;
	t->qent = ent;
	t->d = d;

	if (d->audio.fmt.format == 0) {
		errlog("audio format isn't set");
		ffmem_free(t);
		return NULL;
	}

	if (gg->vol != 100)
		gtrk_vol(gg->vol);

	t->sample_rate = d->audio.fmt.sample_rate;
	t->time_total = ffpcm_time(d->audio.total, t->sample_rate) / 1000;
	wmain_newtrack(ent, t->time_total, d);

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

/** Set seek position. */
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

/** Set volume. */
static void gtrk_vol(uint pos)
{
	if (gg->curtrk == NULL)
		return;

	double db;
	if (pos <= 100)
		db = ffpcm_vol2db(pos, 48);
	else
		db = ffpcm_vol2db_inc(pos - 100, 25, 6);
	gg->curtrk->d->audio.gain = db * 100;
	wmain_status("Volume: %.02FdB", db);
}