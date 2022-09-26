/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <util/path.h>
#include <FFOS/dir.h>
#include <FFOS/process.h>


const fmed_core *core;
ggui *gg;


//FMEDIA MODULE
static const void* gui_iface(const char *name);
static int gui_conf(const char *name, fmed_conf_ctx *ctx);
static int gui_sig(uint signo);
static void gui_destroy(void);
static const fmed_mod fmed_gui_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&gui_iface, &gui_sig, &gui_destroy, &gui_conf
};

#include <gui-gtk/track.h>

static FFTHDCALL int gui_worker(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_corecmd_handler(void *param);
static void corecmd_run(uint cmd, void *udata);
static void file_showpcm(void);
static void file_del(void);
static void lists_load(void);
static void lists_save(void);
static void list_add(const ffstr *fn, int plid);
static void list_rmitems(void);
static void showdir_selected(void);
static void showdir(const char *fn);
static char* userpath(const char *fn);
static int sig_prepare();
static void sig_destroy();

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


static int conf_ctxany(fmed_conf *fc, void *obj)
{
	fmed_conf_skipctx(fc);
	return 0;
}

static int conf_any(fmed_conf *fc, void *obj, const ffstr *val)
{
	return 0;
}

static int conf_list_col_width(fmed_conf *fc, void *obj, int64 val)
{
	struct gui_conf *c = obj;
	if (c->list_col_width_idx >= FF_COUNT(c->list_col_width))
		return FMC_EBADVAL;
	c->list_col_width[c->list_col_width_idx++] = val;
	return 0;
}

int conf_file_delete_method(fmed_conf *fc, void *obj, const ffstr *val)
{
	if (ffstr_eqz(val, "default"))
		gg->conf.file_delete_method = FDM_TRASH;
	else if (ffstr_eqz(val, "rename"))
		gg->conf.file_delete_method = FDM_RENAME;
	else
		return FMC_EBADVAL;
	return 0;
}

static const fmed_conf_arg gui_conf_args[] = {
	{ "seek_step",	FMC_INT8NZ, FMC_O(struct gui_conf, seek_step_delta) },
	{ "seek_leap",	FMC_INT8NZ, FMC_O(struct gui_conf, seek_leap_delta) },
	{ "autosave_playlists",	FMC_BOOL8, FMC_O(struct gui_conf, autosave_playlists) },
	{ "random",	FMC_BOOL8, FMC_O(struct gui_conf, list_random) },
	{ "list_repeat",	FMC_INT8, FMC_O(struct gui_conf, list_repeat) },
	{ "auto_attenuate_ceiling",	FMC_FLOAT32S, FMC_O(struct gui_conf, auto_attenuate_ceiling) },
	{ "list_columns_width",	FMC_INT16_LIST, FMC_F(conf_list_col_width) },
	{ "list_track",	FMC_INT32, FMC_O(struct gui_conf, list_actv_trk_idx) },
	{ "list_scroll",	FMC_INT32, FMC_O(struct gui_conf, list_scroll_pos) },
	{ "file_delete_method",	FMC_STR, FMC_F(conf_file_delete_method) },
	{ "editor_path",	FMC_STRZ, FMC_O(struct gui_conf, editor_path) },

	{ "ydl_format",	FMC_STRZ, FMC_O(struct gui_conf, ydl_format) },
	{ "ydl_outdir",	FMC_STRZ, FMC_O(struct gui_conf, ydl_outdir) },

	{ "record",	FMC_OBJ, FMC_F(conf_ctxany) },
	{ "convert",	FMC_OBJ, FMC_F(conf_convert) },
	{ "global_hotkeys",	FMC_OBJ, FMC_F(conf_ctxany) },
	{ "explorer",	FMC_OBJ, FMC_F(wmain_exp_conf) },

	{ "*",	FMC_STR_MULTI, FMC_F(conf_any) },
	{}
};

static int gui_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (ffsz_eq(name, "gui")) {
		gg->conf.seek_step_delta = 5;
		gg->conf.seek_leap_delta = 60;
		gg->conf.autosave_playlists = 1;
		fmed_conf_addctx(ctx, &gg->conf, gui_conf_args);
		return 0;
	}
	return -1;
}

void dlgs_init();

static int gui_start()
{
	if (NULL == (gg->qu = core->getmod("#queue.queue"))) {
		return 1;
	}
	gg->qu->cmd(FMED_QUE_SETONCHANGE, &gui_que_onchange);

	if (NULL == (gg->track = core->getmod("#core.track"))) {
		return 1;
	}

	fflk_setup();
	if (FFSEM_INV == (gg->sem = ffsem_open(NULL, 0, 0)))
		return 1;

	if (FFTHD_INV == (gg->th = ffthd_create(&gui_worker, gg, 0))) {
		gg->load_err = 1;
		goto end;
	}

	ffsem_wait(gg->sem, -1); //give the GUI thread some time to create controls
	sig_prepare();
	wlog_run();
end:
	ffsem_close(gg->sem);
	gg->sem = FFSEM_INV;
	return gg->load_err;
}

static int gui_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		if (NULL == (gg = ffmem_new(ggui)))
			return -1;
		gg->vol = 100;
		gg->go_pos = -1;
		gg->kqsig = FFKQSIG_NULL;
		dlgs_init();
		return 0;

	case FMED_OPEN:
		return gui_start();

	case FMED_STOP:
		ffthd_join(gg->th, -1, NULL);
		break;
	}
	return 0;
}

static void conf_destroy()
{
	struct gui_conf *c = &gg->conf;
	ffmem_free(c->ydl_format);
	ffmem_free(c->ydl_outdir);
	ffmem_free(c->editor_path);
}

void dlgs_destroy();
static void gui_destroy(void)
{
	if (gg == NULL)
		return;

	dlgs_destroy();
	conf_destroy();
	ffmem_free(gg->home_dir);
	ffsem_close(gg->sem);
	sig_destroy();
	ffmem_free0(gg);
}


static const ffui_ldr_ctl top_ctls[] = {
	FFUI_LDR_CTL(ggui, mfile),
	FFUI_LDR_CTL(ggui, mlist),
	FFUI_LDR_CTL(ggui, mplay),
	FFUI_LDR_CTL(ggui, mconvert),
	FFUI_LDR_CTL(ggui, mhelp),
	FFUI_LDR_CTL(ggui, mexplorer),
	FFUI_LDR_CTL(ggui, mpopup),
	FFUI_LDR_CTL(ggui, mminfo_addtag),
	FFUI_LDR_CTL(ggui, dlg),
	FFUI_LDR_CTL3_PTR(ggui, wmain, wmain_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wabout, wabout_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wcmd, wcmd_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wconvert, wconvert_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wdload, wdload_ctls),
	FFUI_LDR_CTL3_PTR(ggui, winfo, winfo_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wlog, wlog_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wplayprops, wplayprops_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wrename, wrename_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wuri, wuri_ctls),
	FFUI_LDR_CTL_END
};

void dlgs_init()
{
	wmain_init();
	wabout_init();
	wcmd_init();
	wconvert_init();
	wdload_init();
	winfo_init();
	wlog_init();
	wplayprops_init();
	wrename_init();
	wuri_init();
}

void dlgs_destroy()
{
	wconv_destroy();
	wdload_destroy();
	wlog_destroy();
	wmain_destroy();
	winfo_destroy();
	ffmem_free(gg->wabout);
	ffmem_free(gg->wcmd);
	ffmem_free(gg->wplayprops);
	ffmem_free(gg->wrename);
	ffmem_free(gg->wuri);
}

static void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *g = udata;
	return ffui_ldr_findctl(top_ctls, g, name);
}

static const char *const action_str[] = {
#define ACTION_NAMES
#include "actions.h"
#undef ACTION_NAMES
};

static int gui_getcmd(void *udata, const ffstr *name)
{
	(void)udata;
	for (uint i = 0;  i != FFCNT(action_str);  i++) {
		if (ffstr_eqz(name, action_str[i]))
			return i;
	}
	return 0;
}

static int load_ui()
{
	int r = -1;
	char *fn, *fnconf = NULL;
	ffui_loader ldr;
	ffui_ldr_init2(&ldr, &gui_getctl, &gui_getcmd, gg);
	ffmem_copy(ldr.language, core->props->language, 2);

	fftime tstart;
	if (core->loglev == FMED_LOG_DEBUG)
		tstart = fftime_monotonic();

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
	if (core->loglev == FMED_LOG_DEBUG) {
		fftime tend = fftime_monotonic();
		fftime_sub(&tend, &tstart);
		dbglog("loaded GUI in %Ums", (int64)fftime_to_msec(&tend));
	}

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

	wmain_show();

	if (gg->conf.list_random)
		corecmd_add(_A_LIST_RANDOM, NULL);

	if (gg->conf.autosave_playlists)
		corecmd_add(LOADLISTS, NULL);

	if (gg->conf.list_repeat != 0)
		corecmd_add(_A_PLAY_REPEAT, NULL);

	ffsem_post(gg->sem);
	dbglog("entering UI loop");
	ffui_run();
	dbglog("exited UI loop");

	corecmd_add(A_ONCLOSE, NULL);
	goto done;

err:
	gg->load_err = 1;
done:
	ffui_uninit();
	if (gg->load_err)
		ffsem_post(gg->sem);
	return 0;
}


struct corecmd {
	fftask tsk;
	uint cmd;
	fftask_handler uhandler;
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

static void corecmd_handler2(void *param)
{
	struct corecmd *c = param;
	c->uhandler(c->udata);
	ffmem_free(c);
}

void corecmd_addfunc(fftask_handler func, void *udata)
{
	dbglog("%s func:%p  udata:%p", __func__, func, udata);
	struct corecmd *c = ffmem_new(struct corecmd);
	c->tsk.handler = corecmd_handler2;
	c->tsk.param = c;
	c->uhandler = func;
	c->udata = udata;
	core->task(&c->tsk, FMED_TASK_POST);
}

void gui_list_sel(uint idx)
{
	gg->qu->cmdv(FMED_QUE_SEL, idx);
	uint n = gg->qu->cmdv(FMED_QUE_COUNT);
	wmain_list_set(0, n);
}

static void urls_add_play(struct params_urls_add_play *p)
{
	uint n = gg->qu->cmdv(FMED_QUE_COUNT);
	char **name;
	FFSLICE_WALK(&p->v, name) {
		ffstr s = FFSTR_INITZ(*name);
		list_add(&s, -1);
		ffmem_free(*name);
	}

	if (p->play >= 0) {
		gg->focused = n + p->play;
		corecmd_run(A_PLAY, NULL);
	}
}

static fmed_que_entry* _qent_dup(int cur_list, int idx, int target_list)
{
	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(cur_list, idx);
	fmed_que_entry e = {}, *qent;
	e.url = ent->url;
	e.from = ent->from;
	e.to = ent->to;
	qent = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, target_list, &e);
	return qent;
}

static void list_to_next()
{
	int cur_list = wmain_tab_active();
	if (cur_list < 0)
		return;

	int next_list = cur_list + 1;
	int n_lists = gg->qu->cmdv(FMED_QUE_N_LISTS);
	if (next_list == n_lists) {
		wmain_tab_new(0);
		gg->qu->cmdv(FMED_QUE_NEW, 0);
	}

	ffui_sel *sel = wmain_list_getsel_send();
	int i;
	while ((i = ffui_view_selnext(NULL, sel)) >= 0) {
		if (NULL == _qent_dup(cur_list, i, next_list))
			goto end;
	}

end:
	ffui_view_sel_free(sel);
}

static void corecmd_run(uint cmd, void *udata)
{
	dbglog("%s cmd:%d %s  udata:%p"
		, __func__, cmd, (cmd-1 < FF_COUNT(action_str)) ? action_str[cmd] : "", udata);

	switch ((enum ACTION)cmd) {

	case A_FILE_SHOWPCM:
		file_showpcm();
		break;

	case A_FILE_SHOWDIR:
		showdir_selected();
		break;

	case A_FILE_DELFILE:
		file_del();
		break;

	case A_PLAY:
		gg->qu->cmd(FMED_QUE_PLAY_EXCL, (void*)gg->qu->fmed_queue_item(-1, gg->focused));
		break;

	case A_PLAYPAUSE:
		/*
		'udata' is 'curtrk' at the time of event
		* udata != curtrk: do nothing
		* udata == curtrk:
			* curtrk != NULL: play/pause
			* curtrk == NULL: start playing the last track
		*/
		if (udata != gg->curtrk)
			break;

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
		wmain_status("");
		break;

	case A_STOP_AFTER:
		gg->qu->cmd(FMED_QUE_STOP_AFTER, NULL);
		wmain_status("Stop After Current: set");
		break;

	case A_NEXT:
		gg->qu->cmd(FMED_QUE_NEXT2, NULL);
		break;
	case A_PREV:
		gg->qu->cmd(FMED_QUE_PREV2, NULL);
		break;

	case A_PLAY_REPEAT:
		gg->conf.list_repeat = (gg->conf.list_repeat + 1) % 3;
		// fallthrough
	case _A_PLAY_REPEAT: {
		uint rpt = FMED_QUE_REPEAT_NONE;
		switch (gg->conf.list_repeat) {
		case 1:
			rpt = FMED_QUE_REPEAT_TRACK; break;
		case 2:
			rpt = FMED_QUE_REPEAT_ALL; break;
		}
		gg->qu->cmdv(FMED_QUE_SET_REPEATALL, rpt);
		wmain_status("Repeat: %s", repeat_str[gg->conf.list_repeat]);
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
		if (gg->go_pos != (uint)-1)
			gtrk_seek(A_SEEK, gg->go_pos);
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

	case A_LIST_NEW:
		gg->qu->cmdv(FMED_QUE_NEW, (uint)0);
		gg->qu->cmdv(FMED_QUE_SEL, (uint)(size_t)udata);
		break;
	case A_LIST_DEL:
		gg->qu->cmdv(FMED_QUE_DEL, (uint)(size_t)udata);
		break;
	case A_LIST_SEL:
		gui_list_sel((ffsize)udata);
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
	case A_LIST_TO_NEXT:
		list_to_next();
		break;

	case A_LIST_RMDEAD:
		gg->qu->cmd(FMED_QUE_RMDEAD, NULL);
		break;

	case A_LIST_CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR | FMED_QUE_NO_ONCHANGE, NULL);
		wmain_list_clear();
		break;

	case A_LIST_RANDOM:
		gg->conf.list_random = !gg->conf.list_random;
		// fallthrough
	case _A_LIST_RANDOM:
		gg->qu->cmdv(FMED_QUE_SET_RANDOM, (uint)gg->conf.list_random);
		wmain_status("Random: %d", gg->conf.list_random);
		break;

	case A_LIST_SORTRANDOM: {
		gg->qu->cmdv(FMED_QUE_SORT, (int)-1, "__random", 0);
		uint n = gg->qu->cmdv(FMED_QUE_COUNT);
		wmain_list_update(0, n);
		break;
	}

	case A_LIST_READMETA:
		gg->qu->cmdv(FMED_QUE_EXPAND_ALL);
		break;

	case A_ONDROPFILE: {
		ffstr *d = udata;
		ffstr s = *d;
		ffvec fn = {};
		while (0 == ffui_fdrop_next(&fn, &s)) {
			list_add((ffstr*)&fn, -1);
		}
		ffvec_free(&fn);
		ffstr_free(d);
		ffmem_free(d);
		break;
	}

	case _A_URLS_ADD_PLAY: {
		struct params_urls_add_play *p = udata;
		urls_add_play(p);
		ffvec_free(&p->v);
		ffmem_free(p);
		break;
	}

	case A_URL_ADD: {
		ffstr *s = udata;
		list_add(s, -1);
		ffstr_free(s);
		ffmem_free(s);
		break;
	}

	case A_ONCLOSE:
		if (gg->conf.autosave_playlists)
			lists_save();
		gg->track->cmd(NULL, FMED_TRACK_STOPALL_EXIT);
		break;

	case LOADLISTS:
		lists_load();
		break;

	default:
		FF_ASSERT(0);
	}
}

void gui_showtextfile2(const char *fn)
{
	const char *argv[] = {
		"xdg-open", fn, NULL
	};
	const char *path = "/usr/bin/xdg-open";

	if (gg->conf.editor_path != NULL) {
		argv[0] = gg->conf.editor_path;
		path = gg->conf.editor_path;
	}

	ffps ps = ffps_exec(path, argv, (const char**)environ);
	if (ps == FFPS_INV) {
		syserrlog("ffps_exec", 0);
		return;
	}

	dbglog("spawned editor: %u", (int)ffps_id(ps));
	ffps_close(ps);
}

/** Execute GUI text editor to open a file. */
void gui_showtextfile(uint id)
{
	const char *name = NULL;
	char *fn = NULL;
	switch (id) {
	case A_CONF_EDIT:
		name = FMED_GLOBCONF; break;

	case A_USRCONF_EDIT:
		fn = userpath("fmedia-user.conf"); break;

	case A_FMEDGUI_EDIT:
		name = "fmedia.gui"; break;

	case A_README_SHOW:
		name = "README.txt"; break;

	case A_CHANGES_SHOW:
		name = "CHANGES.txt"; break;

	default:
		return;
	}

	if (fn == NULL && NULL == (fn = core->getpath(name, ffsz_len(name))))
		return;

	gui_showtextfile2(fn);
	ffmem_free(fn);
}

static void showdir_selected(void)
{
	int i;
	ffui_sel *sel = wmain_list_getsel_send();
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);
		showdir(ent->url.ptr);
		break;
	}
	ffui_view_sel_free(sel);
}

static void showdir2(const char *dir)
{
	const char *argv[] = {
		"xdg-open", dir, NULL
	};
	ffps ps = ffps_exec("/usr/bin/xdg-open", argv, (const char**)environ);
	if (ps == FFPS_INV) {
		syserrlog("ffps_exec", 0);
		return;
	}

	dbglog("spawned file manager: %u", (int)ffps_id(ps));
	ffps_close(ps);
}

/** Execute GUI file manager to show the directory with a file. */
static void showdir(const char *fn)
{
	ffstr dir;
	ffpath_split2(fn, ffsz_len(fn), &dir, NULL);

	char *dirz = ffsz_alcopystr(&dir);
	showdir2(dirz);
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
		int r = gg->qu->fmed_queue_save(i, buf.ptr);
		if (r != 0) {
			fffile_rm(buf.ptr);
			break;
		}
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
		if (i != 1) {
			gg->qu->cmdv(FMED_QUE_NEW, 0);
			wmain_tab_new(0);
			list_add((ffstr*)&buf, i - 1);
		} else {
			list_add((ffstr*)&buf, -1);
		}
	}

end:
	ffarr_free(&buf);
}

static void list_add(const ffstr *fn, int plid)
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
	ffmem_zero_obj(&e);
	e.url = *fn;
	if (NULL == (pe = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, plid, &e)))
		return;
	if (plid == -1) {
		uint idx = gg->qu->cmdv(FMED_QUE_ID, pe);
		wmain_ent_added(idx);
	}

	if (t == FMED_FT_DIR || t == FMED_FT_PLIST)
		gg->qu->cmd2(FMED_QUE_EXPAND, pe, 0);
}


/** Remove items from list. */
static void list_rmitems(void)
{
	int i, n = 0;
	ffui_sel *sel = wmain_list_getsel_send();
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
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

	ffui_sel *sel = wmain_list_getsel_send();
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

		fmed_track_obj *trk;
		if (NULL == (trk = gg->track->create(FMED_TRK_TYPE_PCMINFO, ent->url.ptr)))
			break;
		fmed_track_info *ti = gg->track->conf(trk);
		if (ent->from != 0)
			ti->audio.abs_seek = ent->from;
		if (ent->to != 0)
			ti->audio.until = ent->to - ent->from;
		gg->track->cmd(trk, FMED_TRACK_XSTART);
	}
	ffui_view_sel_free(sel);
}

int file_del_rename(char *url)
{
	int r = 1;
	char *fn = ffsz_allocfmt("%s.deleted", url);

	if (fffile_exists(fn)) {
		errlog("can't rename file: %s: target file exists", fn);
	} else if (0 != fffile_rename(url, fn)) {
		syserrlog("can't rename file: %s -> %s", url, fn);
	} else {
		r = 0;
	}

	ffmem_free(fn);
	return r;
}

/*int file_del_del(ffstr url)
{
	if (0 == fffile_remove(url.ptr)) {
		syserrlog("can't delete file: %S", &url);
		return -1;
	}
	return 0;
}*/

int file_del_trash(const char **names, ffsize n)
{
	int r;
	if (0 != (r = ffui_glib_trash(names, n))) {
		errlog("can't move files to trash (KDE): %s...: error code %d"
			, names[0], r);
		return -1;
	}

	dbglog("moved file to trash: %s...", names[0]);
	return 0;
}

/** Delete all selected files. */
static void file_del(void)
{
	int i, n = 0;
	fmed_que_entry *ent;
	ffvec v = {};

	ffui_sel *sel = wmain_list_getsel_send();
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);
		*ffvec_pushT(&v, char*) = ent->url.ptr;
	}

	int r = 0;
	char **url;

	switch (gg->conf.file_delete_method) {
	case FDM_TRASH:
		r = file_del_trash((const char**)v.ptr, v.len);
		break;

	case FDM_RENAME:
		FFSLICE_WALK(&v, url) {
			r |= file_del_rename(*url);
		}
		break;
	}

	sel->off = 0;
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i - n);
		gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ent);
		wmain_ent_removed(i - n);
		n++;
	}

	ffui_view_sel_free(sel);
	if (r == 0)
		wmain_status("Deleted %L files", v.len);
	else
		wmain_status("Couldn't delete some files");
	ffvec_free(&v);
}

static const char *const ctl_setts[] = {
	"wmain.position", "wmain.tvol.value",
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


static char* userpath(const char *fn)
{
	return ffsz_alfmt("%s%s", core->props->user_path, fn);
}

static const char *const usrconf_setts[] = {
	"gui.gui.random",
	"gui.gui.list_columns_width",
	"gui.gui.list_track",
	"gui.gui.list_scroll",
	"gui.gui.list_repeat",
	"gui.gui.auto_attenuate_ceiling",
	"gui.gui.seek_step",
	"gui.gui.seek_leap",
};

/** Write user configuration value. */
static void usrconf_write_val(ffconfw *conf, uint i)
{
	int n;
	switch (i) {
	case 0:
		ffconfw_addint(conf, gg->conf.list_random);
		break;
	case 1:
		wmain_list_cols_width_write(conf);
		break;
	case 2:
		n = gg->qu->cmdv(FMED_QUE_CURID, (int)0);
		ffconfw_addint(conf, n);
		break;
	case 3:
		n = wmain_list_scroll_vert();
		ffconfw_addint(conf, n);
		break;
	case 4:
		ffconfw_addint(conf, gg->conf.list_repeat);
		break;
	case 5:
		ffconfw_addfloat(conf, gg->conf.auto_attenuate_ceiling, 2);
		break;
	case 6:
		ffconfw_addint(conf, gg->conf.seek_step_delta);
		break;
	case 7:
		ffconfw_addint(conf, gg->conf.seek_leap_delta);
		break;
	}
}

/** Write the current GUI settings to a user configuration file.
All unsupported keys or comments are left as is.
Empty lines are removed. */
void usrconf_write(void)
{
	char *fn;
	ffarr buf = {};
	fffd f = FF_BADFD;
	ffconfw conf = {};
	ffconfw_init(&conf, 0);
	byte flags[FFCNT(usrconf_setts)] = {};

	if (NULL == (fn = userpath(FMED_USERCONF)))
		return;

	if (FF_BADFD == (f = fffile_open(fn, FFO_CREATE | FFO_RDWR)))
		goto end;
	uint64 fsz = fffile_size(f);
	if ((int)fsz < 0
		|| NULL == ffarr_alloc(&buf, fsz))
		goto end;
	ssize_t r = fffile_read(f, buf.ptr, buf.cap);
	if (r < 0)
		goto end;
	ffstr in;
	ffstr_set(&in, buf.ptr, r);

	while (in.len != 0) {
		ffstr ln;
		ffstr_nextval3(&in, &ln, '\n' | FFS_NV_CR);
		ffbool found = 0;
		for (uint i = 0;  i != FFCNT(usrconf_setts);  i++) {
			if (ffstr_matchz(&ln, usrconf_setts[i])) {
				found = 1;
				flags[i] = 1;
				ffconfw_addkeyz(&conf, usrconf_setts[i]);
				usrconf_write_val(&conf, i);
				break;
			}
		}

		if (!found) {
			found = wconvert_conf_writeval(&ln, &conf);
		}
		if (!found) {
			found = wdload_conf_writeval(&ln, &conf);
		}
		if (!found)
			found = wmain_exp_conf_writeval(&ln, &conf);

		if (!found && ln.len != 0)
			ffconfw_addline(&conf, &ln);
	}

	for (uint i = 0;  i != FFCNT(usrconf_setts);  i++) {
		if (flags[i])
			continue;
		ffconfw_addkeyz(&conf, usrconf_setts[i]);
		usrconf_write_val(&conf, i);
	}
	wconvert_conf_writeval(NULL, &conf);
	wdload_conf_writeval(NULL, &conf);
	wmain_exp_conf_writeval(NULL, &conf);

	ffconfw_fin(&conf);

	ffstr out;
	ffconfw_output(&conf, &out);
	fffile_seek(f, 0, SEEK_SET);
	fffile_write(f, out.ptr, out.len);
	fffile_trunc(f, out.len);

end:
	ffarr_free(&buf);
	ffmem_free(fn);
	ffconfw_close(&conf);
	fffile_safeclose(f);
}


/** Process events from queue module. */
static void gui_que_onchange(fmed_que_entry *ent, uint flags)
{
	uint idx;

	if (flags & FMED_QUE_MORE)
		return;

	if (ent != NULL && !gg->qu->cmdv(FMED_QUE_ISCURLIST, ent))
		return;

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD_DONE:
		if (gg->conf.list_actv_trk_idx != 0) {
			gg->qu->cmdv(FMED_QUE_SETCURID, 0, gg->conf.list_actv_trk_idx);
			gg->conf.list_actv_trk_idx = 0;
		}

		uint n = gg->qu->cmdv(FMED_QUE_COUNT);
		wmain_list_set(0, n);
		break;

	case FMED_QUE_ONADD:
		idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		wmain_ent_added(idx);
		break;

	case FMED_QUE_ONRM:
		idx = ent->list_index;
		wmain_ent_removed(idx);
		break;

	case FMED_QUE_ONUPDATE:
		idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		wmain_list_update(idx, 0);
		break;

	case FMED_QUE_ONCLEAR:
		break;
	}
}

/** Called by core on SIGCHLD signal */
static void onsig(void *param)
{
	struct ffsig_info info = {};
	int sig = ffkqsig_readinfo(gg->kqsig, NULL, &info);
	dbglog("onsig: %d", sig);
	if (sig != SIGCHLD)
		return;

	int exit_code;
	if (0 != ffps_wait(info.pid, 0, &exit_code))
		return;
	wdload_subps_onsig(&info, exit_code);
}

static int sig_prepare()
{
	ffkev_init(&gg->sigtask);
	gg->sigtask.oneshot = 0;
	gg->sigtask.handler = (ffkev_handler)onsig;
	gg->sigtask.udata = NULL;
	int sigs[] = { SIGCHLD };
	gg->kqsig = ffkqsig_attach(core->kq, sigs, 1, ffkev_ptr(&gg->sigtask));
	if (gg->kqsig == FFKQSIG_NULL)
		syserrlog("ffkqsig_attach");
	return (gg->kqsig == FFKQSIG_NULL);
}

static void sig_destroy()
{
	ffkqsig_detach(gg->kqsig, core->kq);  gg->kqsig = FFKQSIG_NULL;
	ffkev_fin(&gg->sigtask);
}


void ontimer(void *param)
{
	gg->timer_val++;
}

void gui_timer_start()
{
	if (ffint_fetch_add(&gg->timer_refcount, 1) != 0)
		return;
	gg->timer_val++;
	fmed_timer_set(&gg->timer, ontimer, gg);
	core->timer(&gg->timer, 250, 0);
}

void gui_timer_stop()
{
	FF_ASSERT(gg->timer_refcount > 0);
	if (ffint_fetch_add(&gg->timer_refcount, -1) != 1)
		return;
	core->timer(&gg->timer, 0, 0);
}
