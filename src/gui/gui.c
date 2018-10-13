/** GUI.
Copyright (c) 2015 Simon Zolin */

/*
CORE       <-   GUI  <-> QUEUE
  |              |
track: ... -> gui-trk -> ...
*/

#include <fmedia.h>
#include <gui/gui.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/path.h>
#include <FF/gui/loader.h>
#include <FFOS/process.h>
#include <FFOS/dir.h>
#include <FF/sys/wreg.h>


const fmed_core *core;
ggui *gg;
static const fmed_net_http *net;


//FMEDIA MODULE
static const void* gui_iface(const char *name);
static int gui_conf(const char *name, ffpars_ctx *ctx);
static int gui_sig(uint signo);
static void gui_destroy(void);
static const fmed_mod fmed_gui_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&gui_iface, &gui_sig, &gui_destroy, &gui_conf
};

static int gui_install(uint sig);

static FFTHDCALL int gui_worker(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_ghk_reg(void);
static void gui_savelists(void);
static int gui_loadlists(void);
static void upd_done(void *obj);

//GUI-TRACK
static void* gtrk_open(fmed_filt *d);
static int gtrk_process(void *ctx, fmed_filt *d);
static void gtrk_close(void *ctx);
static int gtrk_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_gui = {
	&gtrk_open, &gtrk_process, &gtrk_close
};

//RECORDING TRACK WRAPPER
static void* rec_open(fmed_filt *d);
static int rec_process(void *ctx, fmed_filt *d);
static void rec_close(void *ctx);
static const fmed_filter gui_rec_iface = {
	&rec_open, &rec_process, &rec_close
};


struct ghk_ent {
	uint cmd;
	uint hk;
};

static int gui_conf_ghk_add(ffparser_schem *p, void *obj, ffstr *val);

static const ffpars_arg gui_conf_ghk_args[] = {
	{ "*",	FFPARS_TSTR | FFPARS_FWITHKEY, FFPARS_DST(&gui_conf_ghk_add) },
};

static int gui_conf_ghk(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, gg, gui_conf_ghk_args, FFCNT(gui_conf_ghk_args));
	return 0;
}

static const ffpars_arg gui_conf_args[] = {
	{ "record",	FFPARS_TOBJ, FFPARS_DST(&gui_conf_rec) },
	{ "convert",	FFPARS_TOBJ, FFPARS_DST(&gui_conf_convert) },
	{ "portable_conf",	FFPARS_TBOOL | FFPARS_F8BIT, FFPARS_DSTOFF(ggui, portable_conf) },
	{ "minimize_to_tray",	FFPARS_TBOOL | FFPARS_F8BIT, FFPARS_DSTOFF(ggui, minimize_to_tray) },
	{ "status_tray",	FFPARS_TBOOL | FFPARS_F8BIT, FFPARS_DSTOFF(ggui, status_tray) },
	{ "seek_step",	FFPARS_TINT | FFPARS_F8BIT | FFPARS_FNOTZERO, FFPARS_DSTOFF(ggui, seek_step_delta) },
	{ "seek_leap",	FFPARS_TINT | FFPARS_F8BIT | FFPARS_FNOTZERO, FFPARS_DSTOFF(ggui, seek_leap_delta) },
	{ "autosave_playlists",	FFPARS_TBOOL8, FFPARS_DSTOFF(ggui, autosave_playlists) },
	{ "global_hotkeys",	FFPARS_TOBJ, FFPARS_DST(&gui_conf_ghk) },
};

//LOG
static void gui_log(uint flags, fmed_logdata *ld);
static const fmed_log gui_logger = {
	&gui_log
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_gui_mod;
}


static void gui_corecmd(void *param);
static int gui_getcmd(void *udata, const ffstr *name);


/** Add command/hotkey pair into temporary array.*/
static int gui_conf_ghk_add(ffparser_schem *p, void *obj, ffstr *val)
{
	const ffstr *cmd = &p->vals[0];
	struct ghk_ent *e;

	if (NULL == (e = ffarr_pushgrowT(&gg->ghks, 4, struct ghk_ent)))
		return FFPARS_ESYS;

	if (0 == (e->cmd = gui_getcmd(NULL, cmd))) {
		warnlog(core, NULL, "gui", "invalid command: %S", cmd);
		return FFPARS_EBADVAL;
	}

	if (0 == (e->hk = ffui_hotkey_parse(val->ptr, val->len))) {
		warnlog(core, NULL, "gui", "invalid hotkey: %S", val);
		return FFPARS_EBADVAL;
	}

	return 0;
}


#define add  FFUI_LDR_CTL

static const ffui_ldr_ctl wconvert_ctls[] = {
	add(gui_wconvert, wconvert),
	add(gui_wconvert, mmconv),
	add(gui_wconvert, lfn),
	add(gui_wconvert, eout),
	add(gui_wconvert, boutbrowse),
	add(gui_wconvert, vsets),
	add(gui_wconvert, pnsets),
	add(gui_wconvert, pnout),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wrec_ctls[] = {
	add(gui_wrec, wrec),
	add(gui_wrec, mmrec),
	add(gui_wrec, eout),
	add(gui_wrec, boutbrowse),
	add(gui_wrec, vsets),
	add(gui_wrec, pnsets),
	add(gui_wrec, pnout),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wdev_ctls[] = {
	add(gui_wdev, wnd),
	add(gui_wdev, vdev),
	add(gui_wdev, bok),
	add(gui_wdev, pn),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl winfo_ctls[] = {
	add(gui_winfo, winfo),
	add(gui_winfo, vinfo),
	add(gui_winfo, pninfo),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wgoto_ctls[] = {
	add(gui_wgoto, wgoto),
	add(gui_wgoto, etime),
	add(gui_wgoto, bgo),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wabout_ctls[] = {
	add(gui_wabout, wabout),
	add(gui_wabout, ico),
	add(gui_wabout, labout),
	add(gui_wabout, lurl),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wlog_ctls[] = {
	add(gui_wlog, wlog),
	add(gui_wlog, pnlog),
	add(gui_wlog, tlog),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wuri_ctls[] = {
	add(gui_wuri, wuri),
	add(gui_wuri, turi),
	add(gui_wuri, bok),
	add(gui_wuri, bcancel),
	add(gui_wuri, pnuri),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wfilter_ctls[] = {
	add(gui_wfilter, wnd),
	add(gui_wfilter, ttext),
	add(gui_wfilter, cbfilename),
	add(gui_wfilter, breset),
	add(gui_wfilter, pntext),
	{NULL, 0, NULL}
};

static const ffui_ldr_ctl wmain_ctls[] = {
	add(gui_wmain, wmain),
	add(gui_wmain, bpause),
	add(gui_wmain, bstop),
	add(gui_wmain, bprev),
	add(gui_wmain, bnext),
	add(gui_wmain, lpos),
	add(gui_wmain, tabs),
	add(gui_wmain, tpos),
	add(gui_wmain, tvol),
	add(gui_wmain, vlist),
	add(gui_wmain, stbar),
	add(gui_wmain, pntop),
	add(gui_wmain, pnpos),
	add(gui_wmain, pntabs),
	add(gui_wmain, pnlist),
	add(gui_wmain, tray_icon),
	add(gui_wmain, mm),
	{NULL, 0, NULL}
};

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

	FFUI_LDR_CTL3(ggui, wmain, wmain_ctls),
	FFUI_LDR_CTL3(ggui, wconvert, wconvert_ctls),
	FFUI_LDR_CTL3(ggui, wrec, wrec_ctls),
	FFUI_LDR_CTL3(ggui, wdev, wdev_ctls),
	FFUI_LDR_CTL3(ggui, winfo, winfo_ctls),
	FFUI_LDR_CTL3(ggui, wgoto, wgoto_ctls),
	FFUI_LDR_CTL3(ggui, wlog, wlog_ctls),
	FFUI_LDR_CTL3(ggui, wuri, wuri_ctls),
	FFUI_LDR_CTL3(ggui, wfilter, wfilter_ctls),
	FFUI_LDR_CTL3(ggui, wabout, wabout_ctls),
	{NULL, 0, NULL}
};

#undef add

void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *gg = udata;
	return ffui_ldr_findctl(top_ctls, gg, name);
}


static const char *const scmds[] = {
	"PLAY",
	"PAUSE",
	"STOP",
	"STOP_AFTER",
	"NEXT",
	"PREV",

	"SEEK",
	"SEEKING",
	"FFWD",
	"RWND",
	"LEAP_FWD",
	"LEAP_BACK",
	"GOTO_SHOW",
	"GOTO",
	"GOPOS",
	"SETGOPOS",

	"VOL",
	"VOLUP",
	"VOLDOWN",
	"VOLRESET",

	"REC",
	"REC_SETS",
	"PLAYREC",
	"MIXREC",
	"SHOWRECS",
	"DEVLIST_SHOWREC",
	"DEVLIST_SELOK",

	"SHOWCONVERT",
	"SETCONVPOS_SEEK",
	"SETCONVPOS_UNTIL",
	"OUTBROWSE",
	"CONVERT",
	"CVT_SETS_EDIT",

	"OPEN",
	"ADD",
	"ADDURL",
	"QUE_NEW",
	"QUE_DEL",
	"QUE_SEL",
	"SAVELIST",
	"REMOVE",
	"LIST_RMDEAD",
	"CLEAR",
	"SELALL",
	"SELINVERT",
	"SORT",
	"TO_NXTLIST",
	"SHOWDIR",
	"COPYFN",
	"COPYFILE",
	"DELFILE",
	"SHOWINFO",
	"SHOWPCM",
	"INFOEDIT",
	"FILTER_SHOW",
	"FILTER_APPLY",
	"FILTER_RESET",

	"HIDE",
	"SHOW",
	"QUIT",
	"ABOUT",
	"CHECKUPDATE",

	"CONF_EDIT",
	"USRCONF_EDIT",
	"FMEDGUI_EDIT",
	"README_SHOW",
	"CHANGES_SHOW",

	"OPEN_HOMEPAGE",

	"URL_ADD",
	"URL_CLOSE",
};

static int gui_getcmd(void *udata, const ffstr *name)
{
	uint i;
	(void)udata;
	for (i = 0;  i < FFCNT(scmds);  i++) {
		if (ffstr_eqz(name, scmds[i]))
			return i + 1;
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
	void *udata;
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

void gui_corecmd_op(uint cmd, void *udata)
{
	switch (cmd) {
	case PLAY:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PLAY, udata);
		break;

	case STARTPLAY:
		gg->qu->cmd(FMED_QUE_PLAY, udata);
		break;

	case PAUSE:
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

	case STOP:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		if (gg->curtrk != NULL && gg->curtrk->state == ST_PAUSED)
			gg->wmain.wmain.on_action(&gg->wmain.wmain, PAUSE);
		break;

	case STOP_AFTER:
		gg->qu->cmd(FMED_QUE_STOP_AFTER, NULL);
		break;

	case NEXT:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_NEXT2, (gg->curtrk != NULL) ? gg->curtrk->qent : NULL);
		break;

	case PREV:
		if (gg->curtrk != NULL)
			gg->track->cmd(gg->curtrk->trk, FMED_TRACK_STOP);
		gg->qu->cmd(FMED_QUE_PREV2, (gg->curtrk != NULL) ? gg->curtrk->qent : NULL);
		break;


	case SEEK:
		if (gg->curtrk == NULL)
			break;
		gg->curtrk->d->audio.seek = (size_t)udata * 1000;
		gg->curtrk->d->snd_output_clear = 1;
		gg->curtrk->goback = 1;
		break;

	case VOL:
		if (gg->curtrk == NULL)
			break;
		gg->curtrk->d->audio.gain = gg->vol;
		break;


	case CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR | FMED_QUE_NO_ONCHANGE, NULL);
		ffui_view_clear(&gg->wmain.vlist);
		break;

	case SAVELIST: {
		char *list_fn = udata;
		gg->qu->fmed_queue_save(-1, list_fn);
		ffmem_free(list_fn);
		break;
	}

	case QUIT:
		if (gg->autosave_playlists)
			gui_savelists();
		core->sig(FMED_STOP);
		break;
	}
}

static void gui_que_onchange(fmed_que_entry *e, uint flags)
{
	int idx;

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD:
		if (flags & FMED_QUE_ADD_DONE)
			break;
		//fallthrough
	case FMED_QUE_ONRM:
		if (!gg->qu->cmdv(FMED_QUE_ISCURLIST, e))
			return;
	}

	switch (flags & ~_FMED_QUE_FMASK) {
	case FMED_QUE_ONADD:
		if (flags & FMED_QUE_MORE)
			ffui_redraw(&gg->wmain.vlist, 0);
		else if (flags & FMED_QUE_ADD_DONE) {
			ffui_redraw(&gg->wmain.vlist, 1);
			break;
		}
		gui_media_added(e, 0);
		break;

	case FMED_QUE_ONRM:
		if (-1 == (idx = ffui_view_search(&gg->wmain.vlist, (size_t)e)))
			break;
		gui_media_removed(idx);
		break;

	case FMED_QUE_ONCLEAR:
		ffui_view_clear(&gg->wmain.vlist);
		break;
	}
}

void gui_media_add1(const char *fn)
{
	fmed_que_entry e, *pe;
	int t = core->cmd(FMED_FILETYPE, fn);

	if (t == FMED_FT_UKN)
		return;

	ffmem_tzero(&e);
	ffstr_setz(&e.url, fn);
	if (NULL == (pe = (void*)gg->qu->cmd2(FMED_QUE_ADD | FMED_QUE_NO_ONCHANGE, &e, 0)))
		return;
	gui_media_added(pe, 0);

	if (t == FMED_FT_DIR || t == FMED_FT_PLIST)
		gg->qu->cmd2(FMED_QUE_EXPAND, pe, 0);
}

/** For each selected item in list start a new track to analyze PCM peaks. */
void gui_media_showpcm(void)
{
	int i = -1;
	fmed_que_entry *ent;
	ffui_viewitem it;

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
		ent = (void*)ffui_view_param(&it);

		void *trk;
		if (NULL == (trk = gg->track->create(FMED_TRK_TYPE_PLAYBACK, ent->url.ptr)))
			return;
		fmed_trk *trkconf = gg->track->conf(trk);
		trkconf->pcm_peaks = 1;
		gg->track->cmd(trk, FMED_TRACK_START);
	}
}


static void* rec_open(fmed_filt *d)
{
	ffui_stbar_settextz(&gg->wmain.stbar, 0, "Recording...");
	if (gg->status_tray && !ffui_tray_visible(&gg->wmain.tray_icon)) {
		ffui_tray_seticon(&gg->wmain.tray_icon, &gg->wmain.ico_rec);
		ffui_tray_show(&gg->wmain.tray_icon, 1);
	}
	return FMED_FILT_DUMMY;
}

static void rec_close(void *ctx)
{
	if (gg->rec_trk != NULL) {
		ffui_stbar_settextz(&gg->wmain.stbar, 0, "");
		if (gg->status_tray && !gg->min_tray)
			ffui_tray_show(&gg->wmain.tray_icon, 0);
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
		const char *fn = gg->track->getvalstr(gg->rec_trk, "output");
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

	case MIXREC:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_MIX, NULL);
		break;
	}

	gg->rec_trk = t;
	gg->track->cmd(t, FMED_TRACK_START);
}


static void gui_ghk_reg(void)
{
	struct ghk_ent *e;
	uint i = 0;
	(void)i;

	FFARR_WALKT(&gg->ghks, e, struct ghk_ent) {
		i++;
		if (0 != ffui_wnd_ghotkey_reg(&gg->wmain.wmain, e->hk, e->cmd)) {
			warnlog(core, NULL, "gui", "can't register global hotkey #%u", i);
			continue;
		}

		dbglog(core, NULL, "gui", "registered global hotkey #%u", i);
	}

	ffarr_free(&gg->ghks);
}

char* gui_usrconf_filename(void)
{
	if (!gg->portable_conf)
		return core->env_expand(NULL, 0, GUI_USRCONF);
	return core->getpath(FFSTR(GUI_USRCONF_PORT));
}

/** Save playlists to disk. */
static void gui_savelists(void)
{
	ffarr buf = {};
	char *fn = NULL;

	if (gg->portable_conf)
		fn = core->getpath(FFSTR("."));
	else
		fn = core->env_expand(NULL, 0, GUI_PLIST_PATH);
	if (fn == NULL)
		goto end;

	if (NULL == ffarr_alloc(&buf, ffsz_len(fn) + FFSLEN(GUI_PLIST_NAME) + FFINT_MAXCHARS + 1))
		goto end;

	uint n = 1;
	for (uint i = 0; ; i++) {
		if (i == (uint)gg->itab_convert)
			continue;

		buf.len = 0;
		ffstr_catfmt(&buf, "%s" GUI_PLIST_NAME "%Z", fn, n++);

		if (i == ffui_tab_count(&gg->wmain.tabs)) {
			if (fffile_exists(buf.ptr))
				fffile_rm(buf.ptr);
			break;
		}

		gg->qu->fmed_queue_save(i, buf.ptr);
	}

end:
	ffmem_free(fn);
	ffarr_free(&buf);
}

/** Load playlists saved in the previous session. */
static int gui_loadlists(void)
{
	fffd f = FF_BADFD;
	char *fn;
	char **ps;
	int r = -1;

	if (gg->portable_conf)
		fn = core->getpath(FFSTR(GUI_PLIST_PATH_PORT));
	else
		fn = core->env_expand(NULL, 0, GUI_PLIST_PATH);
	if (fn == NULL)
		goto end;

	if (FF_BADFD == (f = fffile_open(fn, O_RDONLY)))
		goto end;

	if (NULL == (ps = ffarr_push(&gg->filenames, char*)))
		goto end;
	if (NULL == (*ps = ffsz_alcopyz(fn))) {
		gg->filenames.len--;
		goto end;
	}
	gui_corecmd_add(&cmd_add, NULL);
	r = 0;

end:
	if (r != 0)
		FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffmem_safefree(fn);
	return r;
}

static FFTHDCALL int gui_worker(void *param)
{
	char *fn = NULL, *fnconf = NULL;
	ffui_loader ldr;
	ffui_init();
	ffui_wnd_initstyle();
	ffui_ldr_init(&ldr);

	if (NULL == (fn = core->getpath(FFSTR("./fmedia.gui"))))
		goto err;
	if (NULL == (fnconf = gui_usrconf_filename()))
		goto err;
	ldr.getctl = &gui_getctl;
	ldr.getcmd = &gui_getcmd;
	ldr.udata = gg;
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
	ffui_ldr_fin(&ldr);

	wmain_init();
	wabout_init();
	wconvert_init();
	wrec_init();
	wdev_init();
	winfo_init();
	gg->wlog.wlog.hide_on_close = 1;
	wuri_init();
	wfilter_init();
	wgoto_init();

	ffui_dlg_multisel(&gg->dlg);

	gg->vol = gui_getvol() * 100;

	if (gg->autosave_playlists)
		gui_loadlists();

	FF_WRITEONCE(&gg->state, 1);

	gui_ghk_reg();

	ffui_run();
	goto done;

err:
	FF_WRITEONCE(&gg->state, 1);
	gg->load_err = 1;
	ffmem_safefree(fn);
	ffmem_safefree(fnconf);
done:
	ffui_dlg_destroy(&gg->dlg);
	ffui_wnd_destroy(&gg->wmain.wmain);
	ffui_uninit();
	return 0;
}

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

static int gui_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "gui"))
		return gtrk_conf(ctx);
	return -1;
}


/*
fmedia-ver-os-arch.tar.xz
...
*/
#define UPD_URL  FMED_HOMEPAGE "/latest.txt"

struct httpreq {
	void *con;
	ffarr data;
	uint status;
	ffui_handler ondone;
};

static void httpreq_free(struct httpreq *h)
{
	FF_SAFECLOSE(h->con, NULL, net->close);
	ffarr_free(&h->data);
	ffmem_free(h);
}

static void http_sig(void *obj)
{
	struct httpreq *h = obj;
	ffhttp_response *resp;
	ffstr d;
	int r = net->recv(h->con, &resp, &d);
	h->status = r;
	switch (r) {
	case FMED_NET_RESP_RECV:
		ffarr_append(&h->data, d.ptr, d.len);
		break;
	case FMED_NET_DONE:
		ffarr_append(&h->data, d.ptr, d.len);
		ffui_thd_post(h->ondone, h);
		break;
	case FMED_NET_ERR:
		ffui_thd_post(h->ondone, h);
		break;
	}
}

#if defined FF_WIN
#define OS_STR  "win"
#elif defined FF_BSD
#define OS_STR  "bsd"
#else
#define OS_STR  "linux"
#endif

#if defined FF_WIN
	#ifdef FF_64
	#define CPU_STR  "x64"
	#else
	#define CPU_STR  "x86"
	#endif
#else
	#ifdef FF_64
	#define CPU_STR  "amd64"
	#else
	#define CPU_STR  "i686"
	#endif
#endif


static void upd_done(void *obj)
{
	struct httpreq *h = obj;

	switch (h->status) {
	case FMED_NET_DONE: {
		ffstr s, v;
		ffstr_set2(&s, &h->data);
		while (s.len != 0) {
			ffstr_nextval3(&s, &v, '\n');
			if (-1 != ffstr_findz(&v, OS_STR "-" CPU_STR)) {
				ffstr fn = v, pt, ver;
				ffstr_setz(&ver, core->props->version_str);
				ffstr_nextval3(&fn, &pt, '-'); //"fmedia"
				ffstr_nextval3(&fn, &pt, '-'); //"ver"
				int r = ffstr_vercmp(&ver, &pt);
				if (r == FFSTR_VERCMP_1LESS2) {
					ffarr a = {0};
					ffstr_catfmt(&a, "A new version is available: %S\nDownload it from " FMED_HOMEPAGE
						, &v);
					ffui_msgdlg_show("Updates", a.ptr, a.len, FFUI_MSGDLG_INFO);
					ffarr_free(&a);
					goto done;
				}
			}
		}
		ffui_msgdlg_showz("Updates", "You have the latest version", FFUI_MSGDLG_INFO);
		break;
	}
	case FMED_NET_ERR:
		ffui_msgdlg_showz("Updates", "Error while requesting " UPD_URL, FFUI_MSGDLG_ERR);
		break;
	}

done:
	httpreq_free(h);
}

void gui_upd_check(void)
{
	if (net == NULL
		&& (NULL == core->getmod("net.http")
			|| NULL == (net = core->getmod("net.httpif"))))
		return;
	struct httpreq *h;
	if (NULL == (h = ffmem_new(struct httpreq)))
		return;
	h->ondone = &upd_done;
	if (NULL == (h->con = net->request("GET", UPD_URL, 0))) {
		httpreq_free(h);
		return;
	}
	net->sethandler(h->con, &http_sig, h);
	ffstr s = {0};
	net->send(h->con, &s);
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

	char fn[FF_MAXPATH];
	const char *pfn = ffps_filename(fn, sizeof(fn), NULL);
	if (pfn == NULL)
		return FFPARS_ELAST;
	if (NULL == ffpath_split2(pfn, ffsz_len(pfn), &path, NULL))
		return FFPARS_ELAST;

	if (FFWREG_BADKEY == (k = ffwreg_open(HKEY_CURRENT_USER, "Environment", O_RDWR)))
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
		_ffarr_rm(&buf, pos_path - buf.ptr, n, sizeof(char));
	}

	if (0 != ffwreg_writestr(k, "PATH", buf.ptr, buf.len))
		goto end;

	ffenv_update();

	if (sig == FMED_SIG_INSTALL) {
		fffile_fmt(ffstdout, NULL, "Added \"%S\" to user's environment.\n"
			, path);

		buf.len = 0;
		if (0 == ffstr_catfmt(&buf, "%S\\fmedia-gui.exe%Z", &path))
			goto end;
		buf.len--;
		if (NULL == (desktop = core->env_expand(NULL, 0, "%USERPROFILE%\\Desktop\\fmedia.lnk")))
			goto end;
		if (0 != ffui_createlink(buf.ptr, desktop))
			goto end;
		fffile_fmt(ffstdout, NULL, "Created desktop shortcut to \"%S\".\n"
			, &buf);

	} else
		fffile_fmt(ffstdout, NULL, "Removed \"%S\" from user's environment.\n"
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
		ffmem_init();
		if (NULL == (gg = ffmem_tcalloc1(ggui)))
			return -1;
		gg->go_pos = (uint)-1;
		gg->itab_convert = -1;
		fflk_init(&gg->lktrk);
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

		if (NULL == (gg->th = ffthd_create(&gui_worker, gg, 0))) {
			return 1;
		}

		ffatom_waitchange(&gg->state, 0); //give the GUI thread some time to create controls
		return gg->load_err;

	case FMED_SIG_INSTALL:
	case FMED_SIG_UNINSTALL:
		gui_install(signo);
		break;
	}
	return 0;
}

static void gui_destroy(void)
{
	if (gg == NULL)
		return;

	ffarr_free(&gg->ghks);
	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
	ffui_icon_destroy(&gg->wmain.ico);
	ffui_icon_destroy(&gg->wmain.ico_rec);
	ffui_wnd_close(&gg->wmain.wmain);
	ffthd_join(gg->th, -1, NULL);

	cvt_sets_destroy(&gg->conv_sets);
	rec_sets_destroy(&gg->rec_sets);
	ffmem_free(gg);
}


static int gtrk_conf(ffpars_ctx *ctx)
{
	gg->seek_step_delta = 5;
	gg->seek_leap_delta = 60;
	gg->status_tray = 1;
	ffpars_setargs(ctx, gg, gui_conf_args, FFCNT(gui_conf_args));
	return 0;
}

static void* gtrk_open(fmed_filt *d)
{
	gui_trk *g = ffmem_tcalloc1(gui_trk);
	if (g == NULL)
		return NULL;
	g->lastpos = (uint)-1;
	g->d = d;
	g->trk = d->trk;
	g->task.handler = d->handler;
	g->task.param = d->trk;

	g->qent = (void*)fmed_getval("queue_item");
	if (g->qent == FMED_PNULL) {
		ffmem_free(g);
		return FMED_FILT_SKIP; //tracks being recorded are not started from "queue"
	}

	if (FMED_PNULL != d->track->getvalstr(d->trk, "output"))
		g->conversion = 1;
	else {
		fflk_lock(&gg->lktrk);
		gg->curtrk = g;
		fflk_unlock(&gg->lktrk);

		gui_corecmd_op(VOL, NULL);
	}

	g->state = ST_PLAYING;
	return g;
}

static void gtrk_close(void *ctx)
{
	gui_trk *g = ctx;
	core->task(&g->task, FMED_TASK_DEL);
	gui_setmeta(NULL, g->qent);

	if (gg->curtrk == g) {
		fflk_lock(&gg->lktrk);
		gg->curtrk = NULL;
		fflk_unlock(&gg->lktrk);
		gui_clear();
	}
	ffmem_free(g);
}

static int gtrk_process(void *ctx, fmed_filt *d)
{
	gui_trk *g = ctx;
	char buf[255];
	size_t n;
	int64 playpos;
	uint playtime, first = 0;

	if (d->meta_block)
		goto done;

	if (g->sample_rate == 0) {
		if (d->audio.fmt.format == 0) {
			errlog(core, d->trk, NULL, "audio format isn't set");
			return FMED_RERR;
		}

		g->sample_rate = d->audio.fmt.sample_rate;
		g->total_time_sec = ffpcm_time(d->audio.total, g->sample_rate) / 1000;
		first = 1;
	}

	if (g->conversion) {
		gui_conv_progress(g);
		goto done;
	}

	if (first) {
		gui_newtrack(g, d, g->qent);
		d->meta_changed = 0;
	}

	if (g->goback) {
		g->goback = 0;
		return FMED_RMORE;
	}

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	if (d->meta_changed) {
		d->meta_changed = 0;
		gui_setmeta(g, g->qent);
	}

	playpos = d->audio.pos;
	if (playpos == FMED_NULL) {
		goto done;
	}

	playtime = (uint)(ffpcm_time(playpos, g->sample_rate) / 1000);
	if (playtime == g->lastpos)
		goto done;
	g->lastpos = playtime;

	ffui_trk_set(&gg->wmain.tpos, playtime);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u"
		, playtime / 60, playtime % 60
		, g->total_time_sec / 60, g->total_time_sec % 60);
	ffui_settext(&gg->wmain.lpos, buf, n);

done:
	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}


static void gui_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + sizeof(buf) - FFSLEN("\r\n");

	if (ld->tid != 0) {
		s += ffs_fmt(s, end, "%s :%xU [%s] %s: "
			, ld->stime, ld->tid, ld->level, ld->module);
	} else {
		s += ffs_fmt(s, end, "%s [%s] %s: "
			, ld->stime, ld->level, ld->module);
	}
	if (ld->ctx != NULL)
		s += ffs_fmt(s, end, "%S:\t", ld->ctx);
	s += ffs_fmtv(s, end, ld->fmt, ld->va);
	if (flags & FMED_LOG_SYS)
		s += ffs_fmt(s, end, ": %E", fferr_last());
	*s++ = '\r';
	*s++ = '\n';

	ffui_edit_addtext(&gg->wlog.tlog, buf, s - buf);

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER)
		ffui_show(&gg->wlog.wlog, 1);
}
