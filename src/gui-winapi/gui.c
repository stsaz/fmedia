/** GUI.
Copyright (c) 2015 Simon Zolin */

/*
CORE       <-   GUI  <-> QUEUE
  |              |
track: ... -> gui-trk -> ...
*/

#include <gui-winapi/gui.h>
#include <gui-winapi/track.h>

#include <util/array.h>
#include <util/path.h>
#include <util/gui-winapi/loader.h>
#include <FFOS/process.h>
#include <FFOS/win/reg.h>
#include <FFOS/dir.h>


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

static void gui_stop(void);
static int gui_install(uint sig);

static FFTHDCALL int gui_worker(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_savelists(void);
static void gui_loadlists(void);

static int gtrk_conf(fmed_conf_ctx *ctx);

//RECORDING TRACK WRAPPER
static void* rec_open(fmed_filt *d);
static int rec_process(void *ctx, fmed_filt *d);
static void rec_close(void *ctx);
static const fmed_filter gui_rec_iface = {
	&rec_open, &rec_process, &rec_close
};


static int gui_conf_ghk_add(fmed_conf *fc, void *obj, ffstr *val);

static const fmed_conf_arg gui_conf_ghk_args[] = {
	{ "*",	FMC_STR_MULTI, FMC_F(gui_conf_ghk_add) },
	{}
};

static int gui_conf_ghk(fmed_conf *fc, void *obj)
{
	fmed_conf_addnewctx(fc, gg, gui_conf_ghk_args);
	return 0;
}

static int conf_list_col_width(fmed_conf *fc, void *obj, int64 val)
{
	if (gg->list_col_width_idx >= FF_COUNT(gg->list_col_width))
		return FMC_EBADVAL;
	gg->list_col_width[gg->list_col_width_idx++] = val;
	return 0;
}

static int conf_file_delete_method(fmed_conf *fc, void *obj, const ffstr *val)
{
	if (ffstr_eqz(val, "default"))
		gg->fdel_method = FDEL_DEFAULT;
	else if (ffstr_eqz(val, "rename"))
		gg->fdel_method = FDEL_RENAME;
	else
		return FMC_EBADVAL;
	return 0;
}

static const fmed_conf_arg gui_conf_args[] = {
	{ "record",	FMC_OBJ, FMC_F(gui_conf_rec) },
	{ "convert",	FMC_OBJ, FMC_F(gui_conf_convert) },
	{ "minimize_to_tray",	FMC_BOOL8, FMC_O(ggui, minimize_to_tray) },
	{ "status_tray",	FMC_BOOL8, FMC_O(ggui, status_tray) },
	{ "seek_step",	FMC_INT8NZ, FMC_O(ggui, seek_step_delta) },
	{ "seek_leap",	FMC_INT8NZ, FMC_O(ggui, seek_leap_delta) },
	{ "autosave_playlists",	FMC_BOOL8, FMC_O(ggui, autosave_playlists) },
	{ "global_hotkeys",	FMC_OBJ, FMC_F(gui_conf_ghk) },
	{ "theme",	FMC_INT8, FMC_O(ggui, theme_startup) },
	{ "random",	FMC_BOOL8, FMC_O(ggui, list_random) },
	{ "list_repeat",	FMC_INT8, FF_OFF(ggui, conf) + FF_OFF(struct guiconf, list_repeat) },
	{ "auto_attenuate_ceiling",	FMC_FLOAT32S, FF_OFF(ggui, conf) + FF_OFF(struct guiconf, auto_attenuate_ceiling) },
	{ "sel_after_cursor",	FMC_BOOL8, FMC_O(ggui, sel_after_cur) },
	{ "list_columns_width",	FMC_INT16_LIST, FMC_F(conf_list_col_width) },
	{ "file_delete_method",	FMC_STR, FMC_F(conf_file_delete_method) },
	{ "list_track",	FMC_INT32, FMC_O(ggui, list_actv_trk_idx) },
	{ "list_scroll",	FMC_INT32, FMC_O(ggui, list_scroll_pos) },
	{}
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_gui_mod;
}


static void gui_corecmd(void *param);
static int gui_getcmd(void *udata, const ffstr *name);


/** Add command/hotkey pair into temporary array.*/
static int gui_conf_ghk_add(fmed_conf *fc, void *obj, ffstr *val)
{
	const ffstr *cmd = fmed_conf_any_keyname(fc);
	struct ghk_ent *e;

	if (NULL == (e = ffarr_pushgrowT(&gg->ghks, 4, struct ghk_ent)))
		return FMC_ESYS;

	if (0 == (e->cmd = gui_getcmd(NULL, cmd))) {
		warnlog(core, NULL, "gui", "invalid command: %S", cmd);
		return FMC_EBADVAL;
	}

	if (0 == (e->hk = ffui_hotkey_parse(val->ptr, val->len))) {
		warnlog(core, NULL, "gui", "invalid hotkey: %S", val);
		return FMC_EBADVAL;
	}

	return 0;
}


#define add  FFUI_LDR_CTL

static const ffui_ldr_ctl top_ctls[] = {
	add(ggui, mfile),
	add(ggui, mlist),
	add(ggui, mplay),
	add(ggui, mrec),
	add(ggui, mconvert),
	add(ggui, mhelp),
	add(ggui, mtray),
	add(ggui, mlist_popup),
	add(ggui, dlg),

	FFUI_LDR_CTL3_PTR(ggui, wmain, wmain_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wabout, wabout_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wconvert, wconvert_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wdev, wdevlist_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wfilter, wfilter_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wgoto, wgoto_ctls),
	FFUI_LDR_CTL3_PTR(ggui, winfo, winfo_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wlog, wlog_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wplayprops, wplayprops_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wrec, wrec_ctls),
	FFUI_LDR_CTL3_PTR(ggui, wuri, wuri_ctls),
	{NULL, 0, NULL}
};

void dlgs_init()
{
	wmain_init();
	wabout_init();
	wconvert_init();
	wdev_init();
	wfilter_init();
	wgoto_init();
	winfo_init();
	wlog_init();
	wplayprops_init();
	wrec_init();
	wuri_init();
}

void dlgs_destroy()
{
	wmain_destroy();
	wrec_destroy();
	wconvert_destroy();
	ffmem_free(gg->wabout);
	ffmem_free(gg->wdev);
	ffmem_free(gg->wfilter);
	ffmem_free(gg->wgoto);
	ffmem_free(gg->winfo);
	ffmem_free(gg->wlog);
	ffmem_free(gg->wplayprops);
	ffmem_free(gg->wuri);
}

#undef add

void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *gg = udata;
	return ffui_ldr_findctl(top_ctls, gg, name);
}


static const char *const scmds[] = {
#define ACTION_NAMES
#include "actions.h"
#undef ACTION_NAMES
};

static int gui_getcmd(void *udata, const ffstr *name)
{
	uint i;
	(void)udata;
	for (i = 0;  i < FFCNT(scmds);  i++) {
		if (ffstr_eqz(name, scmds[i]))
			return i;
	}
	return 0;
}

void gui_runcmd(const struct cmd *cmd, void *udata)
{
	cmdfunc_u u;
	u.f = cmd->func;
	if (cmd->flags & F0)
		u.f0();
	else if (cmd->flags & CMD_FUDATA)
		u.fudata(cmd->cmd, udata);
	else //F1
		u.f(cmd->cmd);
}

struct corecmd {
	fftask tsk;
	const struct cmd *cmd;
	fftask_handler uhandler;
	void *udata;
	struct cmd cmd_s;
};

static void gui_corecmd(void *param)
{
	struct corecmd *c = param;
	gui_runcmd(c->cmd, c->udata);
	ffmem_free(c);
}

void gui_corecmd_add(const struct cmd *cmd, void *udata)
{
	struct corecmd *c = ffmem_tcalloc1(struct corecmd);
	if (c == NULL) {
		syserrlog(core, NULL, "gui", "alloc");
		return;
	}
	c->tsk.handler = &gui_corecmd;
	c->tsk.param = c;
	c->cmd = cmd;
	c->udata = udata;
	core->task(&c->tsk, FMED_TASK_POST);
}

void gui_corecmd_add2(uint action_id, void *udata)
{
	struct corecmd *c = ffmem_new(struct corecmd);
	c->tsk.handler = &gui_corecmd;
	c->tsk.param = c;
	c->cmd_s.cmd = action_id;
	c->cmd_s.flags = F1 | CMD_FCORE | CMD_FUDATA;
	c->cmd_s.func = gui_corecmd_op;
	c->cmd = &c->cmd_s;
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
	dbglog0("%s func:%p  udata:%p", __func__, func, udata);
	struct corecmd *c = ffmem_new(struct corecmd);
	c->tsk.handler = corecmd_handler2;
	c->tsk.param = c;
	c->uhandler = func;
	c->udata = udata;
	core->task(&c->tsk, FMED_TASK_POST);
}

void gui_media_open(uint id)
{
	const char **pfn;

	if (id == OPEN)
		gui_corecmd_op(A_LIST_CLEAR, NULL);

	FFARR_WALKT(&gg->filenames, pfn, const char*) {
		gui_media_add2(*pfn, -1, ADDF_CHECKTYPE | ADDF_NOUPDATE);
	}

	list_update(0, 0);

	if (id == OPEN)
		gui_corecmd_op(A_PLAY_NEXT, NULL);

	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
}

void gui_corecmd_op(uint cmd, void *udata)
{
	switch (cmd) {
	case PLAY: {
		int focused;
		if (-1 == (focused = ffui_view_focused(&gg->wmain->vlist)))
			break;
		/* Note: we should use 'gg->lktrk' here,
		 but because it's only relevant for non-conversion tracks,
		 and 'curtrk' is modified only within main thread,
		 and this function always runs within main thread,
		 nothing will break here without the lock.
		*/
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PLAY, (void*)gg->qu->fmed_queue_item(-1, focused));
		break;
	}

	case A_PLAY_PAUSE:
		if (gg->curtrk == NULL) {
			gg->qu->cmd(FMED_QUE_PLAY, NULL);
			break;
		}

		if (gg->curtrk->state == ST_PAUSED) {
			gg->curtrk->state = ST_PLAYING;
			gui_status(FFSTR(""));
			gg->curtrk->d->snd_output_pause = 0;
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_UNPAUSE);
			break;
		}

		gg->curtrk->d->snd_output_pause = 1;
		gui_status(FFSTR("Paused"));
		gg->curtrk->state = ST_PAUSED;
		break;

	case A_PLAY_STOP:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		if (gg->curtrk != NULL && gg->curtrk->state == ST_PAUSED)
			gg->wmain->wnd.on_action(&gg->wmain->wnd, A_PLAY_PAUSE);
		break;

	case A_PLAY_STOP_AFTER:
		gg->qu->cmd(FMED_QUE_STOP_AFTER, NULL);
		break;

	case A_PLAY_NEXT:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_NEXT2, (gg->curtrk != NULL) ? gg->curtrk->qent : NULL);
		break;

	case A_PLAY_PREV:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PREV2, (gg->curtrk != NULL) ? gg->curtrk->qent : NULL);
		break;


	case A_PLAY_SEEK:
		if (gg->curtrk == NULL)
			break;
		gg->curtrk->d->audio.seek = (size_t)udata * 1000;
		gg->curtrk->d->snd_output_clear = 1;
		gg->curtrk->goback = 1;
		break;

	case A_PLAY_VOL:
		if (gg->curtrk == NULL)
			break;
		gg->curtrk->d->audio.gain = gg->vol;
		break;

	case A_PLAY_REPEAT: {
		gg->conf.list_repeat = (gg->conf.list_repeat + 1) % 3;
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

	case A_LIST_CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR | FMED_QUE_NO_ONCHANGE, NULL);
		ffui_view_clear(&gg->wmain->vlist);
		break;

	case A_LIST_SAVELIST: {
		char *list_fn = udata;
		gg->qu->fmed_queue_save(-1, list_fn);
		ffmem_free(list_fn);
		break;
	}

	case QUIT:
		if (gg->autosave_playlists)
			gui_savelists();
		if (gg->fav_pl != -1)
			fav_save();
		core->sig(FMED_STOP);
		break;

	case A_LIST_SORTRANDOM: {
		gg->qu->cmdv(FMED_QUE_SORT, (int)-1, "__random", 0);
		list_update(0, 0);
		break;
	}

	case A_LIST_READMETA:
		gg->qu->cmdv(FMED_QUE_EXPAND_ALL);
		break;
	}
}

static void gui_que_onchange(fmed_que_entry *e, uint flags)
{
	int idx;

	if (flags & FMED_QUE_MORE)
		return;

	if (e != NULL && !gg->qu->cmdv(FMED_QUE_ISCURLIST, e))
		return;

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD_DONE:
		uint n = gg->qu->cmdv(FMED_QUE_COUNT);
		ffui_view_setcount(&gg->wmain->vlist, n);

		if (gg->list_actv_trk_idx != 0) {
			gg->qu->cmdv(FMED_QUE_SETCURID, 0, gg->list_actv_trk_idx);
			gg->list_actv_trk_idx = 0;
		}

		if (gg->list_scroll_pos != 0) {
			ffui_view_makevisible(&gg->wmain->vlist, gg->list_scroll_pos);
			gg->list_scroll_pos = 0;
		}
		break;

	case FMED_QUE_ONADD:
		gui_media_added(e);
		break;

	case FMED_QUE_ONRM:
		idx = e->list_index;
		list_update(idx, -1);
		break;

	case FMED_QUE_ONUPDATE:
		idx = gg->qu->cmdv(FMED_QUE_ID, e);
		list_update(idx, 0);
		break;

	case FMED_QUE_ONCLEAR:
		wmain_list_clear();
		break;
	}
}

void gui_media_add1(const char *fn)
{
	gui_media_add2(fn, -1, ADDF_CHECKTYPE);
}

void gui_media_add2(const char *fn, int pl, uint flags)
{
	fmed_que_entry e, *pe;
	int t = FMED_FT_FILE;

	if (flags & ADDF_CHECKTYPE) {
		if (ffs_matchz(fn, ffsz_len(fn), "http://"))
			flags &= ~ADDF_CHECKTYPE;
	}

	if (flags & ADDF_CHECKTYPE) {
		t = core->cmd(FMED_FILETYPE, fn);
		if (t == FMED_FT_UKN)
			return;
	}

	ffmem_zero_obj(&e);
	ffstr_setz(&e.url, fn);
	if (NULL == (pe = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, pl, &e)))
		return;

	if (!(flags & ADDF_NOUPDATE))
		gui_media_added(pe);

	if (t == FMED_FT_DIR || t == FMED_FT_PLIST)
		gg->qu->cmd2(FMED_QUE_EXPAND, pe, 0);
}

/** For each selected item in list start a new track to analyze PCM peaks. */
void gui_media_showpcm(void)
{
	int i = -1;
	fmed_que_entry *ent;

	while (-1 != (i = wmain_list_next_selected(i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

		void *trk;
		if (NULL == (trk = gg->track->create(FMED_TRK_TYPE_PLAYBACK, ent->url.ptr)))
			return;
		fmed_trk *trkconf = gg->track->conf(trk);
		trkconf->pcm_peaks = 1;
		gg->track->cmd(trk, FMED_TRACK_XSTART);
	}
}


static void* rec_open(fmed_filt *d)
{
	wmain_rec_started();
	return FMED_FILT_DUMMY;
}

static void rec_close(void *ctx)
{
	if (gg->rec_trk != NULL) {
		wmain_rec_stopped();
		gg->rec_trk = NULL;
	}
}

static int rec_process(void *ctx, fmed_filt *d)
{
	d->out = d->data,  d->outlen = d->datalen;
	return FMED_RDONE;
}


void gui_rec(uint cmd)
{
	void *t;

	if (gg->rec_trk != NULL) {
		const char *fn = gg->track->getvalstr(gg->rec_trk, "output_expanded");
		if (fn != FMED_PNULL)
			gui_media_add1(fn);
		gg->track->cmd(gg->rec_trk, FMED_TRACK_STOP);
		return;
	}

	if (NULL == (t = gg->track->create(FMED_TRACK_REC, NULL)))
		return;

	gg->track->cmd2(t, FMED_TRACK_ADDFILT_BEGIN, "gui.rec-nfy");

	if (0 != gui_rec_addsetts(t)) {
		gg->track->cmd(t, FMED_TRACK_STOP);
		return;
	}

	switch (cmd) {
	case PLAYREC:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_PLAY, NULL);
		break;

	// case MIXREC:
	// 	gg->track->cmd(NULL, FMED_TRACK_STOPALL);
	// 	gg->qu->cmd(FMED_QUE_MIX, NULL);
	// 	break;
	}

	gg->rec_trk = t;
	gg->track->cmd(t, FMED_TRACK_START);
}


char* gui_usrconf_filename(void)
{
	return gui_userpath(GUI_USERCONF);
}

char* gui_userpath(const char *fn)
{
	return ffsz_alfmt("%s%s", core->props->user_path, fn);
}

static const char *const usrconf_setts[] = {
	"gui.gui.theme",
	"gui.gui.auto_attenuate_ceiling",
	"gui.gui.list_repeat",
	"gui.gui.random",
	"gui.gui.sel_after_cursor",
	"gui.gui.list_columns_width",
	"gui.gui.list_track",
	"gui.gui.list_scroll",
};

/** Write user configuration value. */
static void usrconf_write_val(ffconfw *conf, uint i)
{
	int n;
	switch (i) {
	case 0:
		ffconfw_addint(conf, gg->theme_index);
		break;
	case 1:
		ffconfw_addfloat(conf, gg->conf.auto_attenuate_ceiling, 2);
		break;
	case 2:
		ffconfw_addint(conf, gg->conf.list_repeat);
		break;
	case 3:
		ffconfw_addint(conf, gg->list_random);
		break;
	case 4:
		ffconfw_addint(conf, gg->sel_after_cur);
		break;
	case 5:
		wmain_list_cols_width_write(conf);
		break;
	case 6:
		n = gg->qu->cmdv(FMED_QUE_CURID, (int)0);
		ffconfw_addint(conf, n);
		break;
	case 7:
		n = ffui_view_topindex(&gg->wmain->vlist);
		ffconfw_addint(conf, n);
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
	ffconfw conf;
	ffconfw_init(&conf, FFCONFW_FCRLF);
	byte flags[FFCNT(usrconf_setts)] = {};

	if (NULL == (fn = gui_userpath(FMED_USERCONF)))
		return;

	if (FF_BADFD == (f = fffile_open(fn, FFO_CREATE | O_RDWR)))
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

		if (!found)
			found = wrec_conf_writeval(&ln, &conf);
		if (!found)
			found = wconvert_conf_writeval(&ln, &conf);

		if (!found && ln.len != 0)
			ffconfw_addline(&conf, &ln);
	}

	for (uint i = 0;  i != FFCNT(usrconf_setts);  i++) {
		if (flags[i])
			continue;
		ffconfw_addkeyz(&conf, usrconf_setts[i]);
		usrconf_write_val(&conf, i);
	}
	wrec_conf_writeval(NULL, &conf);
	wconvert_conf_writeval(NULL, &conf);

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

/** Save playlists to disk. */
static void gui_savelists(void)
{
	ffarr buf = {};
	const char *fn = core->props->user_path;

	if (NULL == ffarr_alloc(&buf, ffsz_len(fn) + FFSLEN(GUI_PLIST_NAME) + FFINT_MAXCHARS + 1))
		goto end;

	uint n = 1;
	uint total = (uint)ffui_tab_count(&gg->wmain->tabs);
	for (uint i = 0; ; i++) {
		if (i == (uint)gg->itab_convert
			|| i == (uint)gg->fav_pl)
			continue;

		buf.len = 0;
		ffstr_catfmt(&buf, "%s" GUI_PLIST_NAME "%Z", fn, n++);

		if (i == total) {
			if (fffile_exists(buf.ptr))
				fffile_rm(buf.ptr);
			break;
		}

		gg->qu->fmed_queue_save(i, buf.ptr);
	}

end:
	ffarr_free(&buf);
}

/** Load playlists saved in the previous session. */
static void gui_loadlists(void)
{
	const char *fn = core->props->user_path;
	ffarr buf = {};

	if (NULL == ffarr_alloc(&buf, ffsz_len(fn) + FFSLEN(GUI_PLIST_NAME) + FFINT_MAXCHARS + 1))
		goto end;

	for (uint i = 1;  ;  i++) {
		buf.len = 0;
		ffstr_catfmt(&buf, "%s" GUI_PLIST_NAME "%Z", fn, i);
		if (!fffile_exists(buf.ptr))
			break;
		if (i != 1)
			gui_que_new();
		gui_media_add1(buf.ptr);
	}

end:
	ffarr_free(&buf);
	return;
}

static const struct cmd cmd_loadlists = { 0, F0 | CMD_FCORE, &gui_loadlists };

int gui_files_del(const ffarr *ents)
{
	ffarr names = {};
	fmed_que_entry **pent;
	int r = -1;
	switch (gg->fdel_method) {

	case FDEL_DEFAULT:
		FFARR_WALKT(ents, pent, fmed_que_entry*) {
			char **pname;
			if (NULL == (pname = ffarr_pushgrowT(&names, 16, char*)))
				goto done;
			*pname = (*pent)->url.ptr;
		}

		if (0 != ffui_fop_del((const char *const *)names.ptr, names.len, FFUI_FOP_ALLOWUNDO))
			goto done;

		FFARR_WALKT(ents, pent, fmed_que_entry*) {
			gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, *pent);
		}
		r = 0;
		break;

	case FDEL_RENAME:
		r = 0;
		FFARR_WALKT(ents, pent, fmed_que_entry*) {
			fmed_que_entry *ent = *pent;
			const char *fn = ent->url.ptr;
			char *newfn = ffsz_alfmt("%S.deleted", &ent->url);
			if (newfn == NULL)
				break;

			if (0 == fffile_rename(fn, newfn)) {
				gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ent);
			} else {
				syserrlog(core, NULL, "gui", "file rename: %S", &ent->url);
				r = -1;
			}

			ffmem_free(newfn);
		}
		break;

	default:
		return -1;
	}

done:
	ffarr_free(&names);
	return r;
}

/** Load GUI:
. Read fmedia.gui - create controls
. Read fmedia.gui.conf - set user properties on the controls
. Load user playlists from the previous session
. Enter GUI message processing loop
*/
static FFTHDCALL int gui_worker(void *param)
{
	char *fn = NULL, *fnconf = NULL;
	ffui_loader ldr;
	ffui_init();
	ffui_wnd_initstyle();
	ffui_ldr_init2(&ldr, gui_getctl, gui_getcmd, gg);

	if (NULL == (fn = core->getpath(FFSTR("./fmedia.gui"))))
		goto err;
	if (NULL == (fnconf = gui_usrconf_filename()))
		goto err;
	if (0 != ffui_ldr_loadfile(&ldr, fn)) {
		ffstr3 msg = {0};
		ffstr_catfmt(&msg, "parsing fmedia.gui: %s", ffui_ldr_errstr(&ldr));
		errlog(core, NULL, "gui", "%S", &msg);
		ffui_msgdlg_show("fmedia GUI", msg.ptr, msg.len, FFUI_MSGDLG_ERR);
		ffarr_free(&msg);
		ffui_ldr_fin(&ldr);
		goto err;
	}
	ffui_ldr_loadconf(&ldr, fnconf);
	ffmem_free0(fn);
	ffmem_free0(fnconf);
	gg->paned_array = ldr.paned_array;
	ffui_ldr_fin(&ldr);

	wmain_show();

	ffui_dlg_multisel(&gg->dlg);

	gg->vol = gui_getvol() * 100;

	ffsem_post(gg->sem);

	if (gg->autosave_playlists)
		gui_corecmd_add(&cmd_loadlists, NULL);

	if (gg->conf.list_repeat != 0)
		gui_corecmd_add2(A_PLAY_REPEAT, NULL);

	gui_themes_read();
	gui_themes_add(gg->theme_startup);

	dbglog0("entering UI loop");
	ffui_run();
	dbglog0("leaving UI loop");
	goto done;

err:
	gg->load_err = 1;
	ffmem_safefree(fn);
	ffmem_safefree(fnconf);
done:
	ffui_dlg_destroy(&gg->dlg);
	ffui_wnd_destroy(&gg->wmain->wnd);
	ffui_uninit();
	if (gg->load_err)
		ffsem_post(gg->sem);
	dbglog0("exitting UI worker thread");
	return 0;
}

extern const fmed_filter fmed_gui;
extern const fmed_log gui_logger;
static const void* gui_iface(const char *name)
{
	if (!ffsz_cmp(name, "gui"))
		return &fmed_gui;
	else if (!ffsz_cmp(name, "rec-nfy"))
		return &gui_rec_iface;
	else if (!ffsz_cmp(name, "log"))
		return &gui_logger;
	return NULL;
}

static int gui_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "gui"))
		return gtrk_conf(ctx);
	return -1;
}


/** Read value to a dynamic buffer.
Return value type on success;  <0 on error. */
int ffwreg_readbuf(ffwinreg k, const char *name, ffarr *buf)
{
	int r;
	ffwreg_val val = {0};
	val.data = buf->ptr;
	val.datalen = buf->cap;
	r = ffwreg_read(k, name, &val);
	if (r == 0) {
		if (NULL == ffarr_realloc(buf, val.datalen))
			return -1;

		val.data = buf->ptr;
		val.datalen = buf->cap;
		r = ffwreg_read(k, name, &val);
		if (r == 0)
			return -1;

	} else if (r == 1) {
		if (buf->ptr == NULL) {
			buf->ptr = val.data;
			buf->cap = val.datalen;
		}
	}

	if (r < 0)
		return -1;

	buf->len = val.datalen;
	return val.type;
}

/**
1. HKCU\Environment\PATH = [...;] FMEDIA_PATH [;...]
2. Desktop shortcut to fmedia-gui.exe
*/
static int gui_install(uint sig)
{
	ffwreg k;
	ffarr buf = {0};
	ffstr path;
	char *desktop = NULL;
	int r = -1;

	CoInitializeEx(NULL, 0);

	char fn[FF_MAXPATH];
	const char *pfn = ffps_filename(fn, sizeof(fn), NULL);
	if (pfn == NULL)
		goto end;
	if (NULL == ffpath_split2(pfn, ffsz_len(pfn), &path, NULL))
		goto end;

	if (FFWREG_BADKEY == (k = ffwinreg_open(HKEY_CURRENT_USER, "Environment", FFWINREG_READWRITE)))
		goto end;

	r = ffwreg_readbuf(k, "PATH", &buf);
	if (!ffwreg_isstr(r))
		goto end;

	const char *pos_path = ffs_ifinds(buf.ptr, buf.len, path.ptr, path.len);

	if (sig == FMED_SIG_INSTALL) {
		if (pos_path != ffarr_end(&buf)) {
			errlog(core, NULL, "", "Path \"%S\" is already in user's environment", &path);
			r = 0;
			goto end;
		}

		if ((buf.len == 0 || NULL == ffarr_append(&buf, ";", 1))
			|| NULL == ffarr_append(&buf, path.ptr, path.len))
			goto end;

	} else {
		if (pos_path == ffarr_end(&buf)) {
			r = 0;
			goto end;
		}

		uint n = path.len + 1;
		if (pos_path != buf.ptr && *(pos_path - 1) == ';')
			pos_path--; // "...;\fmedia"
		else if (pos_path + path.len != ffarr_end(&buf) && *(pos_path + path.len) == ';')
		{} // "\fmedia;..."
		else if (buf.len == path.len)
			n = path.len; // "\fmedia"
		else
			goto end;
		ffslice_rm((ffslice*)&buf, pos_path - (char*)buf.ptr, n, sizeof(char));
	}

	if (0 != ffwreg_writestr(k, "PATH", buf.ptr, buf.len))
		goto end;

	ffenv_update();

	if (sig == FMED_SIG_INSTALL) {
		ffstdout_fmt("Added \"%S\" to user's environment.\n"
			, path);

		buf.len = 0;
		if (0 == ffstr_catfmt(&buf, "%S\\fmedia-gui.exe%Z", &path))
			goto end;
		buf.len--;
		if (NULL == (desktop = core->env_expand(NULL, 0, "%USERPROFILE%\\Desktop\\fmedia.lnk")))
			goto end;
		if (0 != ffui_createlink(buf.ptr, desktop))
			goto end;
		ffstdout_fmt("Created desktop shortcut to \"%S\".\n"
			, &buf);

	} else
		ffstdout_fmt("Removed \"%S\" from user's environment.\n"
			, path);

	r = 0;
end:
	ffmem_safefree(desktop);
	ffwreg_close(k);
	ffarr_free(&buf);
	if (r != 0)
		syserrlog(core, NULL, "", "%s", (sig == FMED_SIG_INSTALL) ? "install" : "uninstall");
	return 0;
}

static int gui_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		if (NULL == (gg = ffmem_tcalloc1(ggui)))
			return -1;
		gg->go_pos = (uint)-1;
		gg->sort_col = -1;
		gg->itab_convert = -1;
		gg->fav_pl = -1;
		dlgs_init();
		fflk_init(&gg->lktrk);
		fflk_init(&gg->lklog);
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
		if (FFSEM_INV == (gg->sem = ffsem_open(NULL, 0, 0)))
			return 1;

		if (NULL == (gg->th = ffthd_create(&gui_worker, gg, 0))) {
			gg->load_err = 1;
			goto end;
		}

		ffsem_wait(gg->sem, -1); //give the GUI thread some time to create controls
end:
		ffsem_close(gg->sem);
		gg->sem = FFSEM_INV;
		return gg->load_err;

	case FMED_STOP:
		gui_stop();
		break;

	case FMED_SIG_INSTALL:
	case FMED_SIG_UNINSTALL:
		gui_install(signo);
		break;
	}
	return 0;
}

static void gui_stop(void)
{
	ffui_wnd_close(&gg->wmain->wnd);
	if (0 != ffthd_join(gg->th, 3000, NULL))
		syserrlog0("ffthd_join");
	ffarr_free(&gg->ghks);
	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
}

static void gui_destroy(void)
{
	if (gg == NULL)
		return;

	ffui_paned **pn;
	FFSLICE_WALK_T(&gg->paned_array, pn, ffui_paned*) {
		ffmem_free(*pn);
	}
	ffvec_free(&gg->paned_array);

	gui_themes_destroy();
	dlgs_destroy();
	ffsem_close(gg->sem);
	ffmem_free0(gg);
}


static int gtrk_conf(fmed_conf_ctx *ctx)
{
	gg->seek_step_delta = 5;
	gg->seek_leap_delta = 60;
	gg->status_tray = 1;
	fmed_conf_addctx(ctx, gg, gui_conf_args);
	return 0;
}
