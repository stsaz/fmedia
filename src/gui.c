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
#include <FFOS/win/reg.h>


const fmed_core *core;
ggui *gg;


//FMEDIA MODULE
static const void* gui_iface(const char *name);
static int gui_sig(uint signo);
static void gui_destroy(void);
static const fmed_mod fmed_gui_mod = {
	&gui_iface, &gui_sig, &gui_destroy
};

static int gui_install(uint sig);

static FFTHDCALL int gui_worker(void *param);
static void gui_task(void *param);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_rec(uint cmd);

//GUI-TRACK
static void* gtrk_open(fmed_filt *d);
static int gtrk_process(void *ctx, fmed_filt *d);
static void gtrk_close(void *ctx);
static int gtrk_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_gui = {
	&gtrk_open, &gtrk_process, &gtrk_close, &gtrk_conf
};

static int gui_conf_rec_dir(ffparser_schem *ps, void *obj, ffstr *val);
static const ffpars_arg gui_conf[] = {
	{ "rec_dir",	FFPARS_TSTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DST(&gui_conf_rec_dir) },
	{ "rec_format",	FFPARS_TSTR | FFPARS_FCOPY, FFPARS_DSTOFF(ggui, rec_format) },
};

//LOG
static void gui_log(const char *stime, const char *module, const char *level, const ffstr *id,
	const char *fmt, va_list va);
static const fmed_log gui_logger = {
	&gui_log
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	ffmem_init();
	core = _core;
	return &fmed_gui_mod;
}


static int gui_getcmd(void *udata, const ffstr *name);

typedef struct {
	const char *name;
	uint off;
} name_to_ctl;

#define add(wnd, name) { #name, FFOFF(wnd, name) }

static const name_to_ctl wconvert_ctls[] = {
	add(gui_wconvert, wconvert),
	add(gui_wconvert, mmconv),
	add(gui_wconvert, eout),
	add(gui_wconvert, boutbrowse),
	add(gui_wconvert, vsets),
	add(gui_wconvert, pnsets),
	add(gui_wconvert, pnout),
};

static const name_to_ctl winfo_ctls[] = {
	add(gui_winfo, winfo),
	add(gui_winfo, vinfo),
	add(gui_winfo, pninfo),
};

static const name_to_ctl wabout_ctls[] = {
	add(gui_wabout, wabout),
	add(gui_wabout, labout),
};

static const name_to_ctl wlog_ctls[] = {
	add(gui_wlog, wlog),
	add(gui_wlog, pnlog),
	add(gui_wlog, tlog),
};

static const name_to_ctl wmain_ctls[] = {
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
};

static const name_to_ctl top_ctls[] = {
	add(ggui, mfile),
	add(ggui, mplay),
	add(ggui, mrec),
	add(ggui, mconvert),
	add(ggui, mhelp),
	add(ggui, mtray),
	add(ggui, dlg),
};

#undef add
#define add(name, ctls)  { #name, FFOFF(ggui, name), ctls, FFCNT(ctls) }

struct name_to_wnd {
	const char *name;
	uint off;
	const name_to_ctl *ctls;
	uint nctls;
};

static const struct name_to_wnd wnds[] = {
	add(wmain, wmain_ctls),
	add(wconvert, wconvert_ctls),
	add(winfo, winfo_ctls),
	add(wlog, wlog_ctls),
	add(wabout, wabout_ctls),
};

#undef add

void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *gg = udata;
	uint i, nctls, goff = 0;
	ffstr wndname, ctlname;
	const name_to_ctl *ctls = NULL;

	if (NULL == ffs_split2by(name->ptr, name->len, '.', &wndname, &ctlname))
		ctlname = wndname;

	for (i = 0;  i != FFCNT(wnds);  i++) {
		if (ffstr_eqz(&wndname, wnds[i].name)) {
			ctls = wnds[i].ctls;
			nctls = wnds[i].nctls;
			goff = wnds[i].off;
			break;
		}
	}
	if (ctls == NULL) {
		if (wndname.ptr != ctlname.ptr)
			return NULL;
		ctls = top_ctls;
		nctls = FFCNT(top_ctls);
	}

	for (i = 0;  i != nctls;  i++) {
		if (ffstr_eqz(&ctlname, ctls[i].name)) {
			ffui_ctl *c = (void*)((char*)gg + goff + ctls[i].off);
			if (ctls[i].name[0] != 'm')
				c->name = ctls[i].name;
			return c;
		}
	}
	return NULL;
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
	"GOPOS",
	"SETGOPOS",

	"VOL",
	"VOLUP",
	"VOLDOWN",

	"REC",
	"PLAYREC",
	"MIXREC",
	"SHOWRECS",

	"SHOWCONVERT",
	"OUTBROWSE",
	"CONVERT",
	"CVT_SETS_EDIT",

	"OPEN",
	"ADD",
	"QUE_NEW",
	"QUE_DEL",
	"QUE_SEL",
	"SAVELIST",
	"REMOVE",
	"CLEAR",
	"SELALL",
	"SELINVERT",
	"SORT",
	"SHOWDIR",
	"COPYFN",
	"COPYFILE",
	"DELFILE",
	"SHOWINFO",
	"INFOEDIT",

	"HIDE",
	"SHOW",
	"QUIT",
	"ABOUT",

	"CONF_EDIT",
	"USRCONF_EDIT",
	"FMEDGUI_EDIT",
	"README_SHOW",
	"CHANGES_SHOW",
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

static void gui_task(void *param)
{
	uint cmd = (uint)(size_t)param;
	switch (cmd) {
	case PLAY:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_PLAY, gg->play_id);
		break;

	case PAUSE:
		gg->qu->cmd(FMED_QUE_PLAY, NULL);
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
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_NEXT, NULL);
		break;

	case PREV:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_PREV, NULL);
		break;


	case SAVELIST:
		gg->qu->cmd(FMED_QUE_SAVE, gg->list_fn);
		ffmem_free0(gg->list_fn);
		break;


	case REC:
	case PLAYREC:
	case MIXREC:
		gui_rec(cmd);
		break;

	case QUE_NEW:
		gui_que_new();
		break;
	case QUE_DEL:
		gui_que_del();
		break;
	case QUE_SEL:
		gui_que_sel();
		break;

	case QUIT:
		core->sig(FMED_STOP);
		break;
	}
}

void gui_task_add(uint id)
{
	gg->cmdtask.param = (void*)(size_t)id;
	core->task(&gg->cmdtask, FMED_TASK_POST);
}

void gui_addcmd(cmdfunc2 func, uint cmd)
{
	if (gg->curtrk == NULL)
		return;

	fflk_lock(&gg->curtrk->lkcmds);
	struct cmd *pcmd = ffarr_push(&gg->curtrk->cmds, struct cmd);
	if (pcmd != NULL) {
		pcmd->cmd = cmd;
		pcmd->func = func;
	}
	fflk_unlock(&gg->curtrk->lkcmds);
}

static void gui_que_onchange(fmed_que_entry *e, uint flags)
{
	int idx;

	switch (flags) {
	case FMED_QUE_ONADD:
		gui_media_added(e);
		break;

	case FMED_QUE_ONRM:
		if (-1 == (idx = ffui_view_search(&gg->wmain.vlist, (size_t)e)))
			break;
		gui_media_removed(idx);
		break;
	}
}

void gui_media_add1(const char *fn)
{
	fmed_que_entry e, *pe;

	ffmem_tzero(&e);
	ffstr_setz(&e.url, fn);
	if (NULL == (pe = (void*)gg->qu->cmd2(FMED_QUE_ADD | FMED_QUE_NO_ONCHANGE, &e, 0)))
		return;
	gui_media_added(pe);
}

static void gui_rec(uint cmd)
{
	void *t;
	ffstr3 nm = {0};
	fftime now;
	ffdtm dt;

	if (gg->rec_trk != NULL) {
		const char *fn = gg->track->getvalstr(gg->rec_trk, "output");
		gg->track->cmd(gg->rec_trk, FMED_TRACK_STOP);
		gg->rec_trk = NULL;
		gui_status(FFSTR(""));
		if (fn != FMED_PNULL)
			gui_media_add1(fn);
		return;
	}

	if (0 != ffdir_make(gg->rec_dir) && fferr_last() != EEXIST) {
		char buf[1024];
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Can't create directory for recordings:\n%s", gg->rec_dir);
		ffui_msgdlg_show("fmedia GUI", buf, n, FFUI_MSGDLG_ERR);
		return;
	}

	if (NULL == (t = gg->track->create(FMED_TRACK_REC, NULL)))
		return;

	fftime_now(&now);
	fftime_split(&dt, &now, FFTIME_TZLOCAL);
	ffstr_catfmt(&nm, "%s%crec-%u-%02u-%02u_%02u%02u%02u.%S%Z"
		, gg->rec_dir, FFPATH_SLASH, dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, &gg->rec_format);
	gg->track->setvalstr4(t, "output", nm.ptr, FMED_TRK_FACQUIRE);

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

	gg->track->cmd(t, FMED_TRACK_START);
	gg->rec_trk = t;

	gui_status(FFSTR("Recording..."));
}


static FFTHDCALL int gui_worker(void *param)
{
	char *fn, *fnconf;
	ffui_loader ldr;
	ffui_init();
	ffui_wnd_initstyle();
	ffui_ldr_init(&ldr);

	if (NULL == (fn = core->getpath(FFSTR("./fmedia.gui"))))
		goto err;
	if (NULL == (fnconf = ffenv_expand(NULL, 0, GUI_USRCONF))) {
		ffmem_free(fn);
		goto err;
	}
	ldr.getctl = &gui_getctl;
	ldr.getcmd = &gui_getcmd;
	ldr.udata = gg;
	if (0 != ffui_ldr_loadfile(&ldr, fn)) {
		ffstr3 msg = {0};
		ffstr_catfmt(&msg, "parsing fmedia.gui: %s", ffui_ldr_errstr(&ldr));
		errlog(core, NULL, "gui", "%S", &msg);
		ffui_msgdlg_show("fmedia GUI", msg.ptr, msg.len, FFUI_MSGDLG_ERR);
		ffarr_free(&msg);
		ffmem_free(fn);
		ffmem_free(fnconf);
		ffui_ldr_fin(&ldr);
		goto err;
	}
	ffui_ldr_loadconf(&ldr, fnconf);
	ffmem_free(fn);
	ffmem_free(fnconf);
	ffui_ldr_fin(&ldr);

	wmain_init();
	wabout_init();
	wconvert_init();
	winfo_init();
	gg->wlog.wlog.hide_on_close = 1;

	gg->cmdtask.handler = &gui_task;
	ffui_dlg_multisel(&gg->dlg);

	fflk_unlock(&gg->lk);

	ffui_run();
	goto done;

err:
	gg->load_err = 1;
	fflk_unlock(&gg->lk);

done:
	ffui_dlg_destroy(&gg->dlg);
	ffui_wnd_destroy(&gg->wmain.wmain);
	ffui_uninit();
	return 0;
}

static const void* gui_iface(const char *name)
{
	if (!ffsz_cmp(name, "gui")) {
		if (NULL == (gg = ffmem_tcalloc1(ggui)))
			return NULL;
		gg->go_pos = (uint)-1;

		return &fmed_gui;

	} else if (!ffsz_cmp(name, "log")) {
		return &gui_logger;
	}
	return NULL;
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

	if (FFWREG_BADKEY == (k = ffwreg_open(HKEY_CURRENT_USER, "Environment", KEY_ALL_ACCESS)))
		goto end;

	if (-1 == ffwreg_readbuf(k, "PATH", &buf))
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
		if (NULL == (desktop = ffenv_expand(NULL, 0, "%USERPROFILE%\\Desktop\\fmedia.lnk")))
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
	case FMED_OPEN:
		if (NULL == (gg->qu = core->getmod("#queue.queue"))) {
			return 1;
		}
		fflk_init(&gg->lktrk);
		gg->qu->cmd(FMED_QUE_SETONCHANGE, &gui_que_onchange);

		if (NULL == (gg->track = core->getmod("#core.track"))) {
			return 1;
		}

		fflk_setup();
		fflk_lock(&gg->lk);

		if (NULL == (gg->th = ffthd_create(&gui_worker, gg, 0))) {
			return 1;
		}

		fflk_lock(&gg->lk); //give the GUI thread some time to create controls
		fflk_unlock(&gg->lk);
		return gg->load_err;

	case FMED_GUI_SHOW:
		gg->wmain.wmain.on_action(&gg->wmain.wmain, SHOW);
		break;

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
	ffui_wnd_close(&gg->wmain.wmain);
	ffthd_join(gg->th, -1, NULL);
	core->task(&gg->cmdtask, FMED_TASK_DEL);
	ffmem_safefree(gg->rec_dir);

	ffstr_free(&gg->rec_format);
	ffmem_free(gg);
}


static int gui_conf_rec_dir(ffparser_schem *ps, void *obj, ffstr *val)
{
	if (NULL == (gg->rec_dir = ffenv_expand(NULL, 0, val->ptr)))
		return FFPARS_ESYS;
	ffmem_free(val->ptr);
	return 0;
}

static int gtrk_conf(ffpars_ctx *ctx)
{
	ffstr_copy(&gg->rec_format, "wav", 3);
	ffpars_setargs(ctx, gg, gui_conf, FFCNT(gui_conf));
	return 0;
}

static void* gtrk_open(fmed_filt *d)
{
	fmed_que_entry *plid;
	int64 total_samples;
	gui_trk *g = ffmem_tcalloc1(gui_trk);
	if (g == NULL)
		return NULL;
	fflk_init(&g->lkcmds);
	g->lastpos = (uint)-1;
	g->seekpos = (uint)-1;
	g->trk = d->trk;
	g->task.handler = d->handler;
	g->task.param = d->trk;

	g->sample_rate = (int)fmed_getval("pcm_sample_rate");
	total_samples = fmed_getval("total_samples");
	g->total_time_sec = ffpcm_time(total_samples, g->sample_rate) / 1000;
	ffui_trk_setrange(&gg->wmain.tpos, g->total_time_sec);

	plid = (void*)fmed_getval("queue_item");
	if (plid == FMED_PNULL)
		return FMED_FILT_SKIP; //tracks being recorded are not started from "queue"

	gui_newtrack(g, d, plid);

	fflk_lock(&gg->lktrk);
	gg->curtrk = g;
	fflk_unlock(&gg->lktrk);

	if (FMED_PNULL != d->track->getvalstr(d->trk, "output"))
		g->conversion = 1;

	gg->wmain.wmain.on_action(&gg->wmain.wmain, VOL);

	fflk_lock(&gg->lk);
	g->state = ST_PLAYING;
	fflk_unlock(&gg->lk);
	return g;
}

static void gtrk_close(void *ctx)
{
	gui_trk *g = ctx;
	core->task(&g->task, FMED_TASK_DEL);
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
	uint playtime;

	if (g->cmds.len != 0) {
		uint i;
		fflk_lock(&g->lkcmds);
		const struct cmd *pcmd = (void*)g->cmds.ptr;
		for (i = 0;  i != g->cmds.len;  i++) {
			cmdfunc2 f = pcmd[i].func;
			f(g, pcmd[i].cmd);
		}
		g->cmds.len = 0;
		fflk_unlock(&g->lkcmds);

		if (g->goback) {
			g->goback = 0;
			return FMED_RMORE;
		}
	}

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	fflk_lock(&gg->lk);
	switch (g->state) {
	case ST_PAUSE:
		d->track->setval(d->trk, "snd_output_pause", 1);
		g->state = ST_PAUSED;
		fflk_unlock(&gg->lk);
		d->outlen = 0;
		return FMED_ROK;

	case ST_PAUSED:
		gui_status(FFSTR("Paused"));
		fflk_unlock(&gg->lk);
		return FMED_RASYNC;
	}
	fflk_unlock(&gg->lk);

	playpos = fmed_getval("current_position");
	if (playpos == FMED_NULL) {
		d->out = d->data;
		d->outlen = d->datalen;
		return FMED_RDONE;
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


static void gui_log(const char *stime, const char *module, const char *level, const ffstr *id,
	const char *fmt, va_list va)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + sizeof(buf) - FFSLEN("\r\n");

	s += ffs_fmt(s, end, "%s %s %s: ", stime, level, module);
	if (id != NULL)
		s += ffs_fmt(s, end, "%S:\t", id);
	s += ffs_fmtv(s, end, fmt, va);
	*s++ = '\r';
	*s++ = '\n';

	ffui_edit_addtext(&gg->wlog.tlog, buf, s - buf);

	if (!ffsz_cmp(level, "error"))
		ffui_show(&gg->wlog.wlog, 1);
}
