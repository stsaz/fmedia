/** GUI.
Copyright (c) 2015 Simon Zolin */

/*
CORE       <-   GUI  <-> QUEUE
  |              |
track: ... -> gui-trk -> ...
*/

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/path.h>
#include <FF/gui/loader.h>
#include <FFOS/thread.h>
#include <FFOS/process.h>
#include <FFOS/dir.h>


typedef struct gui_trk gui_trk;

typedef struct ggui {
	fflock lktrk;
	gui_trk *curtrk;
	fflock lk;
	const fmed_queue *qu;
	const fmed_track *track;
	fftask cmdtask;
	void *play_id;
	char *rec_dir;
	ffstr rec_format;
	uint load_err;

	void *rec_trk;

	char *conv_dir;
	char *conv_name;
	char *conv_fmt;

	ffui_wnd wmain;
	ffui_menu mm;
	ffui_menu mfile
		, mplay
		, mrec
		, mconvert
		, mhelp
		, mtray;
	ffui_dialog dlg;
	ffui_trkbar tpos
		, tvol;
	ffui_view vlist;
	ffui_paned pntop
		, pnlist;
	ffui_ctl stbar;
	ffui_trayicon tray_icon;

	ffui_wnd wconvert;
	ffui_menu mmconv;
	ffui_edit eout;
	ffui_btn boutbrowse;

	ffui_wnd winfo;
	ffui_view vinfo;
	ffui_paned pninfo;

	ffui_wnd wabout;
	ffui_ctl labout;

	ffui_wnd wlog;
	ffui_paned pnlog;
	ffui_edit tlog;

	ffthd th;
} ggui;

typedef void (*cmdfunc0)(void);
typedef void (*cmdfunc)(uint id);
typedef void (*cmdfunc2)(gui_trk *g, uint id);

enum CMDFLAGS {
	F1 = 0,
	F0 = 1,
	F2 = 2,
};

struct cmd {
	uint cmd;
	uint flags; // enum CMDFLAGS
	void *func; // cmdfunc*
};

enum LIST_HDR {
	H_IDX,
	H_ART,
	H_TIT,
	H_DUR,
	H_INF,
	H_FN,
};

enum {
	VINFO_NAME,
	VINFO_VAL,
};

enum ST {
	ST_PLAYING = 1,
	ST_PAUSE,
	ST_PAUSED,
};

struct gui_trk {
	uint state;
	uint lastpos;
	uint sample_rate;
	uint total_time_sec;
	uint gain;

	fflock lkcmds;
	ffarr cmds; //struct cmd[]

	void *trk;
	fftask task;
};

#define GUI_USRCONF  "%APPDATA%/fmedia/fmedia.gui.conf"

static const fmed_core *core;
static ggui *gg;

//FMEDIA MODULE
static const void* gui_iface(const char *name);
static int gui_sig(uint signo);
static void gui_destroy(void);
static const fmed_mod fmed_gui_mod = {
	&gui_iface, &gui_sig, &gui_destroy
};

static FFTHDCALL int gui_worker(void *param);
static void gui_action(ffui_wnd *wnd, int id);
static void gui_clear(void);
static void gui_status(const char *s, size_t len);
static void gui_list_add(ffui_viewitem *it, size_t par);
static int __stdcall gui_list_sortfunc(LPARAM p1, LPARAM p2, LPARAM udata);
static void gui_task_add(uint id);
static void gui_task(void *param);
static void gui_media_added(fmed_que_entry *ent);
static void gui_media_add1(const char *fn);
static void gui_media_open(uint id);
static void gui_media_removed(uint i);
static void gui_media_remove(void);
static fmed_que_entry* gui_list_getent(void);
static void gui_media_seek(gui_trk *g, uint cmd);
static void gui_vol(uint id);
static void gui_media_vol(gui_trk *g, uint id);
static void gui_media_showdir(void);
static void gui_media_copyfn(void);
static void gui_media_fileop(uint cmd);
static void gui_media_showinfo(void);
static void gui_on_dropfiles(ffui_wnd *wnd, ffui_fdrop *df);
static void gui_que_onchange(fmed_que_entry *e, uint flags);
static void gui_rec(uint cmd);
static void gui_onclose(void);

static void gui_showconvert(void);
static void gui_cvt_action(ffui_wnd *wnd, int id);
static void gui_conv_browse(void);
static void gui_convert(void);

static void gui_info_action(ffui_wnd *wnd, int id);

static void gui_addcmd(cmdfunc2 func, uint cmd);

//GUI-TRACK
static void* gtrk_open(fmed_filt *d);
static int gtrk_process(void *ctx, fmed_filt *d);
static void gtrk_close(void *ctx);
static int gtrk_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_gui = {
	&gtrk_open, &gtrk_process, &gtrk_close, &gtrk_conf
};

static int gui_conf_rec_dir(ffparser_schem *ps, void *obj, ffstr *val);
static int gui_conf_conv_dir(ffparser_schem *ps, void *obj, char *val);
static const ffpars_arg gui_conf[] = {
	{ "rec_dir",	FFPARS_TSTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DST(&gui_conf_rec_dir) },
	{ "rec_format",	FFPARS_TSTR | FFPARS_FCOPY, FFPARS_DSTOFF(ggui, rec_format) },

	{ "convert_dir",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DST(&gui_conf_conv_dir) },
	{ "convert_name",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DSTOFF(ggui, conv_name) },
	{ "convert_format",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DSTOFF(ggui, conv_fmt) },
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


static void* gui_getctl(void *udata, const ffstr *name);
static int gui_getcmd(void *udata, const ffstr *name);

typedef struct {
	const char *name;
	uint off;
} name_to_ctl;

#define add(name) { #name, FFOFF(ggui, name) }
static const name_to_ctl ctls[] = {
	add(wmain),
	add(tpos),
	add(tvol),
	add(vlist),
	add(stbar),
	add(pntop),
	add(pnlist),
	add(dlg),
	add(tray_icon),

	add(mm),
	add(mfile),
	add(mplay),
	add(mrec),
	add(mconvert),
	add(mhelp),
	add(mtray),

	add(wconvert),
	add(mmconv),
	add(eout),
	add(boutbrowse),

	add(winfo),
	add(vinfo),
	add(pninfo),

	add(wabout),
	add(labout),

	add(wlog),
	add(pnlog),
	add(tlog),
};
#undef add

static void* gui_getctl(void *udata, const ffstr *name)
{
	ggui *gg = udata;
	uint i;
	for (i = 0;  i < FFCNT(ctls);  i++) {
		if (ffstr_eqz(name, ctls[i].name))
			return (byte*)gg + ctls[i].off;
	}
	return NULL;
}

enum CMDS {
	PLAY = 1,
	PAUSE,
	STOP,
	NEXT,
	PREV,

	SEEK,
	SEEKING,
	FFWD,
	RWND,

	VOL,
	VOLUP,
	VOLDOWN,

	REC,
	PLAYREC,
	MIXREC,
	SHOWRECS,

	SHOWCONVERT,
	OUTBROWSE,
	CONVERT,

	OPEN,
	ADD,
	REMOVE,
	CLEAR,
	SELALL,
	SELINVERT,
	SORT,
	SHOWDIR,
	COPYFN,
	COPYFILE,
	DELFILE,
	SHOWINFO,
	INFOEDIT,

	HIDE,
	SHOW,
	QUIT,
	ABOUT,

	//private:
	ONCLOSE,
};

static const char *const scmds[] = {
	"PLAY",
	"PAUSE",
	"STOP",
	"NEXT",
	"PREV",

	"SEEK",
	"SEEKING",
	"FFWD",
	"RWND",

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

	"OPEN",
	"ADD",
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
			gui_action(&gg->wmain, PAUSE);
		break;

	case NEXT:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_NEXT, NULL);
		break;

	case PREV:
		gg->track->cmd(NULL, FMED_TRACK_STOPALL);
		gg->qu->cmd(FMED_QUE_PREV, NULL);
		break;


	case REC:
	case PLAYREC:
	case MIXREC:
		gui_rec(cmd);
		break;

	case QUIT:
		core->sig(FMED_STOP);
		break;
	}
}

static void gui_task_add(uint id)
{
	gg->cmdtask.param = (void*)(size_t)id;
	core->task(&gg->cmdtask, FMED_TASK_POST);
}

static void gui_addcmd(cmdfunc2 func, uint cmd)
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

static const struct cmd cmds[] = {
	{ STOP,	F1,	&gui_task_add },
	{ NEXT,	F1,	&gui_task_add },
	{ PREV,	F1,	&gui_task_add },

	{ SEEK,	F2,	&gui_media_seek },
	{ FFWD,	F2,	&gui_media_seek },
	{ RWND,	F2,	&gui_media_seek },

	{ VOL,	F1,	&gui_vol },
	{ VOLUP,	F1,	&gui_vol },
	{ VOLDOWN,	F1,	&gui_vol },

	{ REC,	F1,	&gui_task_add },
	{ PLAYREC,	F1,	&gui_task_add },
	{ MIXREC,	F1,	&gui_task_add },

	{ SHOWCONVERT,	F0,	&gui_showconvert },
	{ OUTBROWSE,	F0,	&gui_conv_browse },

	{ OPEN,	F1,	&gui_media_open },
	{ ADD,	F1,	&gui_media_open },
	{ REMOVE,	F0,	&gui_media_remove },
	{ SHOWDIR,	F0,	&gui_media_showdir },
	{ COPYFN,	F0,	&gui_media_copyfn },
	{ COPYFILE,	F1,	&gui_media_fileop },
	{ DELFILE,	F1,	&gui_media_fileop },
	{ SHOWINFO,	F0,	&gui_media_showinfo },
};

static const struct cmd* getcmd(uint cmd)
{
	size_t i, start = 0;
	uint n = FFCNT(cmds);
	while (start != n) {
		i = start + (n - start) / 2;
		if (cmd == cmds[i].cmd) {
			return &cmds[i];
		} else if (cmd < cmds[i].cmd)
			n = i;
		else
			start = i + 1;
	}
	return NULL;
}

static void gui_action(ffui_wnd *wnd, int id)
{
	gui_trk *g = gg->curtrk;

	const struct cmd *cmd = getcmd(id);
	if (cmd != NULL) {
		union {
			cmdfunc0 f0;
			cmdfunc f;
		} u;
		u.f = cmd->func;

		if (cmd->flags & F2) {
			fflk_lock(&gg->lktrk);
			gui_addcmd(cmd->func, id);
			fflk_unlock(&gg->lktrk);

		} else if (cmd->flags & F0)
			u.f0();

		else
			u.f(id);
		return;
	}

	switch (id) {

	case PLAY:
		if (NULL == (gg->play_id = gui_list_getent()))
			break;
		gui_task_add(id);
		break;

	case PAUSE:
		if (g == NULL) {
			gui_task_add(id);
			break;
		}
		fflk_lock(&gg->lk);
		switch (g->state) {
		case ST_PLAYING:
			g->state = ST_PAUSE;
			break;

		case ST_PAUSE:
			g->state = ST_PLAYING;
			break;

		case ST_PAUSED:
			g->state = ST_PLAYING;
			gui_status(FFSTR(""));
			core->task(&g->task, FMED_TASK_POST);
			break;
		}
		fflk_unlock(&gg->lk);
		break;


	case SEEKING:
		{
		uint pos = ffui_trk_val(&gg->tpos);
		char buf[64];
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Seek to %u:%02u"
			, pos / 60, pos % 60);
		gui_status(buf, n);
		}
		break;


	case SHOWRECS:
		ffui_openfolder((const char *const *)&gg->rec_dir, 0);
		break;


	case SELALL:
		ffui_view_sel(&gg->vlist, -1);
		break;

	case SELINVERT:
		ffui_view_sel_invert(&gg->vlist);
		break;

	case SORT:
		if (gg->vlist.col == H_TIT || gg->vlist.col == H_ART || gg->vlist.col == H_FN)
			ffui_view_sort(&gg->vlist, &gui_list_sortfunc, gg->vlist.col);
		break;

	case CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR, NULL);
		ffui_view_clear(&gg->vlist);
		break;

	case HIDE:
		ffui_tray_show(&gg->tray_icon, 1);
		ffui_show(&gg->wmain, 0);
		break;

	case SHOW:
		ffui_show(&gg->wmain, 1);
		ffui_wnd_setfront(&gg->wmain);
		ffui_tray_show(&gg->tray_icon, 0);
		break;

	case ABOUT:
		ffui_show(&gg->wabout, 1);
		break;

	case QUIT:
		ffui_wnd_close(&gg->wmain);
		// break;

	case ONCLOSE:
		gui_task_add(QUIT);
		gui_onclose();
		break;
	}
}

static void gui_info_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case INFOEDIT: {
		int i = ffui_view_selnext(&gg->vinfo, -1);
		ffui_view_edit(&gg->vinfo, i, VINFO_VAL);
		}
		break;
	}
}

static void gui_cvt_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case CONVERT:
		gui_convert();
		break;
	}
}

static int __stdcall gui_list_sortfunc(LPARAM p1, LPARAM p2, LPARAM udata)
{
	fmed_que_entry *e1 = (void*)p1, *e2 = (void*)p2;
	ffstr *s1, *s2, nm;

	switch (udata) {
	case H_ART:
	case H_TIT:
		if (udata == H_ART)
			ffstr_setcz(&nm, "artist");
		else
			ffstr_setcz(&nm, "title");

		s1 = gg->qu->meta_find(e1, nm.ptr, nm.len);
		s2 = gg->qu->meta_find(e2, nm.ptr, nm.len);
		if (s1 == NULL || s2 == NULL) {
			if (s1 == NULL && s2 == NULL)
				return 0;
			else
				return (s1 == NULL) ? 1 : -1;
		}
		return ffstr_cmp2(s1, s2);

	case H_FN:
		return ffstr_cmp2(&e1->url, &e2->url);
	}

	return 0;
}

static void gui_onclose(void)
{
	ffui_pos pos;
	char buf[128], *fn;
	size_t n;
	ffui_loaderw ldr = {0};

	if (NULL == (fn = ffenv_expand(NULL, 0, GUI_USRCONF)))
		return;

	if (IsWindowVisible(gg->wmain.h) && !IsIconic(gg->wmain.h)) {
		ffui_getpos(gg->wmain.h, &pos);
		n = ffs_fmt(buf, buf + sizeof(buf), "%d %d %u %u", pos.x, pos.y, pos.cx, pos.cy);
		ffui_ldr_set(&ldr, "wmain.position", buf, n);
	}

	n = ffs_fmt(buf, buf + sizeof(buf), "%u", ffui_trk_val(&gg->tvol));
	ffui_ldr_set(&ldr, "tvol.value", buf, n);

	if (0 != ffui_ldr_write(&ldr, fn) && fferr_nofile(fferr_last())) {
		if (0 != ffdir_make_path(fn) && fferr_last() != EEXIST) {
			syserrlog(core, NULL, "gui", "Can't create directory for the file: %s", fn);
			goto done;
		}
		if (0 != ffui_ldr_write(&ldr, fn))
			syserrlog(core, NULL, "gui", "Can't write configuration file: %s", fn);
	}

done:
	ffmem_free(fn);
	ffui_ldrw_fin(&ldr);
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
		gg->qu->cmd(FMED_QUE_PLAY, NULL);
		break;

	case MIXREC:
		gg->qu->cmd(FMED_QUE_MIX, NULL);
		break;
	}

	gg->track->cmd(t, FMED_TRACK_START);
	gg->rec_trk = t;

	gui_status(FFSTR("Recording..."));
}

static void gui_showconvert(void)
{
	ffstr3 s = {0};

	if (0 == ffui_view_selcount(&gg->vlist))
		return;

	ffstr_catfmt(&s, "%s%c%s.%s", gg->conv_dir, FFPATH_SLASH, gg->conv_name, gg->conv_fmt);
	ffui_settext(&gg->eout, s.ptr, s.len);
	ffarr_free(&s);

	ffui_show(&gg->wconvert, 1);
	ffui_wnd_setfront(&gg->wconvert);
}

static void gui_conv_browse(void)
{
	const char *fn;
	ffstr fullname;

	ffui_textstr(&gg->eout, &fullname);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &gg->wmain, fullname.ptr, fullname.len)))
		goto done;

	ffui_settextz(&gg->eout, fn);

done:
	ffstr_free(&fullname);
}

static void gui_convert(void)
{
	int i = -1;
	ffui_viewitem it;
	fmed_que_entry e, *qent, *inp;
	ffstr fn;
	uint play = 0;

	ffui_textstr(&gg->eout, &fn);

	while (-1 != (i = ffui_view_selnext(&gg->vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->vlist, 0, &it);
		inp = (void*)ffui_view_param(&it);

		ffmemcpy(&e, inp, sizeof(fmed_que_entry));
		if (NULL != (qent = gg->qu->add(&e))) {
			gg->qu->meta_set(qent, FFSTR("output"), fn.ptr, fn.len, 0);
			if (!play) {
				play = 1;
				gg->play_id = qent;
			}
		}
	}

	if (play)
		gui_task_add(PLAY);
	ffstr_free(&fn);
}

static void gui_que_onchange(fmed_que_entry *e, uint flags)
{
	int idx;

	switch (flags) {
	case FMED_QUE_ONADD:
		gui_media_added(e);
		break;

	case FMED_QUE_ONRM:
		if (-1 == (idx = ffui_view_search(&gg->vlist, (size_t)e)))
			break;
		gui_media_removed(idx);
		break;
	}
}

static void gui_media_seek(gui_trk *g, uint cmd)
{
	switch (cmd) {
	case FFWD:
		ffui_trk_move(&gg->tpos, FFUI_TRK_PGUP);
		break;

	case RWND:
		ffui_trk_move(&gg->tpos, FFUI_TRK_PGDN);
		break;
	}

	gg->track->setval(g->trk, "seek_time", ffui_trk_val(&gg->tpos) * 1000);
	gg->track->setval(g->trk, "snd_output_clear", 1);
}

static void gui_vol(uint id)
{
	char buf[64];
	uint pos;
	double db;
	size_t n;

	switch (id) {
	case VOLUP:
		ffui_trk_move(&gg->tvol, FFUI_TRK_PGUP);
		break;

	case VOLDOWN:
		ffui_trk_move(&gg->tvol, FFUI_TRK_PGDN);
		break;
	}

	pos = ffui_trk_val(&gg->tvol);
	if (pos <= 100)
		db = ffpcm_vol2db(pos, 48);
	else
		db = ffpcm_vol2db_inc(pos - 100, 25, 6);
	n = ffs_fmt(buf, buf + sizeof(buf), "Volume: %.02FdB", db);
	gui_status(buf, n);

	fflk_lock(&gg->lktrk);
	if (gg->curtrk != NULL) {
		gg->curtrk->gain = db * 100;
		gui_addcmd(&gui_media_vol, id);
	}
	fflk_unlock(&gg->lktrk);
}

static void gui_media_vol(gui_trk *g, uint id)
{
	gg->track->setval(gg->curtrk->trk, "gain", g->gain);
}

static void gui_media_showdir(void)
{
	const fmed_que_entry *ent;

	if (NULL == (ent = gui_list_getent()))
		return;

	ffui_openfolder((const char *const *)&ent->url.ptr, 1);
}

/** Copy to clipboard filenames of selected items:
/path/file1 CRLF
/path/file2 */
static void gui_media_copyfn(void)
{
	int i = -1;
	fmed_que_entry *ent;
	ffui_viewitem it;
	ffarr buf = {0};

	while (-1 != (i = ffui_view_selnext(&gg->vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->vlist, 0, &it);
		ent = (void*)ffui_view_param(&it);

		if (0 == ffstr_catfmt(&buf, "%S" FF_NEWLN, &ent->url))
			goto done;
	}

	if (buf.len == 0)
		goto done;

	ffui_clipbd_set(buf.ptr, buf.len - FFSLEN(FF_NEWLN));

done:
	ffarr_free(&buf);
}

static void gui_media_fileop(uint cmd)
{
	int i = -1;
	fmed_que_entry *ent, **pent;
	ffui_viewitem it;
	struct { FFARR(char*) } buf = {0};
	struct { FFARR(fmed_que_entry*) } ents = {0};
	char st[255];
	size_t n;
	char **pitem;

	while (-1 != (i = ffui_view_selnext(&gg->vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->vlist, 0, &it);
		ent = (void*)ffui_view_param(&it);

		if (NULL == (pitem = ffarr_push(&buf, char*)))
			goto done;
		*pitem = ent->url.ptr;

		switch (cmd) {
		case DELFILE:
			if (NULL == (pent = ffarr_push(&ents, fmed_que_entry*)))
				goto done;
			*pent = ent;
			break;
		}
	}

	if (buf.len == 0)
		goto done;

	switch (cmd) {
	case COPYFILE:
		if (0 == ffui_clipbd_setfile((const char *const *)buf.ptr, buf.len)) {
			n = ffs_fmt(st, st + sizeof(st), "Copied %L files to clipboard", buf.len);
			gui_status(st, n);
		}
		break;

	case DELFILE:
		if (0 == ffui_fop_del((const char *const *)buf.ptr, buf.len, FFUI_FOP_ALLOWUNDO)) {
			ffui_redraw(&gg->vlist, 0);
			FFARR_WALK(&ents, pent) {
				gg->qu->cmd(FMED_QUE_RM, *pent);
			}
			ffui_redraw(&gg->vlist, 1);
			n = ffs_fmt(st, st + sizeof(st), "Deleted %L files", buf.len);
			gui_status(st, n);
		}
		break;
	}

done:
	ffarr_free(&buf);
}

static void gui_media_showinfo(void)
{
	fmed_que_entry *e;
	ffui_viewitem it;
	int i;
	ffstr name, *val;

	ffui_show(&gg->winfo, 1);

	if (-1 == (i = ffui_view_selnext(&gg->vlist, -1))) {
		ffui_view_clear(&gg->vinfo);
		return;
	}

	ffui_view_iteminit(&it);
	ffui_view_setindex(&it, i);
	ffui_view_setparam(&it, 0);
	ffui_view_get(&gg->vlist, 0, &it);
	e = (void*)ffui_view_param(&it);

	ffui_settextstr(&gg->winfo, &e->url);

	ffui_redraw(&gg->vinfo, 0);
	ffui_view_clear(&gg->vinfo);
	for (i = 0;  NULL != (val = gg->qu->meta(e, i, &name, 0));  i++) {
		ffui_view_iteminit(&it);
		ffui_view_settextstr(&it, &name);
		ffui_view_append(&gg->vinfo, &it);

		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->vinfo, 1, &it);
	}
	ffui_redraw(&gg->vinfo, 1);
}

static void gui_media_added(fmed_que_entry *ent)
{
	ffstr name;
	ffui_viewitem it;
	ffmem_tzero(&it);
	gui_list_add(&it, (size_t)ent);
	ffui_view_settextstr(&it, &ent->url);
	ffui_view_set(&gg->vlist, H_FN, &it);

	ffpath_split2(ent->url.ptr, ent->url.len, NULL, &name);
	ffui_view_settextstr(&it, &name);
	ffui_view_set(&gg->vlist, H_TIT, &it);
}

static void gui_media_add1(const char *fn)
{
	fmed_que_entry e;

	ffmem_tzero(&e);
	ffstr_setz(&e.url, fn);
	gg->qu->add(&e);
}

static void gui_media_open(uint id)
{
	const char *fn;

	if (NULL == (fn = ffui_dlg_open(&gg->dlg, &gg->wmain)))
		return;

	if (id == OPEN)
		gg->qu->cmd(FMED_QUE_CLEAR, NULL);

	ffui_redraw(&gg->vlist, 0);

	do {
		gui_media_add1(fn);

	} while (NULL != (fn = ffui_dlg_nextname(&gg->dlg)));

	ffui_redraw(&gg->vlist, 1);

	if (id == OPEN)
		gui_task_add(NEXT);
}

static void gui_media_removed(uint i)
{
	ffui_viewitem it;
	char buf[FFINT_MAXCHARS];
	size_t n;

	ffui_redraw(&gg->vlist, 0);
	ffui_view_rm(&gg->vlist, i);

	for (;  ;  i++) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		n = ffs_fromint(i + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		if (0 != ffui_view_set(&gg->vlist, H_IDX, &it))
			break;
	}

	ffui_redraw(&gg->vlist, 1);
}

static void gui_media_remove(void)
{
	int i;
	void *id;
	ffui_viewitem it;

	ffui_redraw(&gg->vlist, 0);

	while (-1 != (i = ffui_view_selnext(&gg->vlist, -1))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->vlist, 0, &it);
		id = (void*)ffui_view_param(&it);
		gg->qu->cmd(FMED_QUE_RM, id);
	}

	ffui_redraw(&gg->vlist, 1);
}

static fmed_que_entry* gui_list_getent(void)
{
	int focused;
	ffui_viewitem it = {0};
	size_t entid;
	if (-1 == (focused = ffui_view_focused(&gg->vlist)))
		return NULL;
	ffui_view_setindex(&it, focused);
	ffui_view_setparam(&it, 0);
	ffui_view_get(&gg->vlist, 0, &it);
	if (0 == (entid = ffui_view_param(&it)))
		return NULL;
	return (void*)entid;
}

static void gui_list_add(ffui_viewitem *it, size_t par)
{
	char buf[FFINT_MAXCHARS];
	size_t n = ffs_fromint(ffui_view_nitems(&gg->vlist) + 1, buf, sizeof(buf), 0);
	ffui_view_settext(it, buf, n);
	ffui_view_setparam(it, par);
	ffui_view_append(&gg->vlist, it);
}

static void gui_status(const char *s, size_t len)
{
	ffui_stbar_settext(&gg->stbar, 1, s, len);
}

static void gui_clear(void)
{
	ffui_settextz(&gg->wmain, "fmedia");
	ffui_trk_set(&gg->tpos, 0);
	ffui_stbar_settext(&gg->stbar, 0, NULL, 0);
	gui_status("", 0);
}

static void gui_on_dropfiles(ffui_wnd *wnd, ffui_fdrop *df)
{
	const char *fn;

	ffui_redraw(&gg->vlist, 0);

	while (NULL != (fn = ffui_fdrop_next(df))) {
		gui_media_add1(fn);
	}

	ffui_redraw(&gg->vlist, 1);
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

	gg->wmain.top = 1;
	gg->wmain.on_action = &gui_action;
	gg->wmain.onclose_id = ONCLOSE;
	ffui_settextz(&gg->labout, "fmedia v" FMED_VER "\nhttp://fmedia.firmdev.com");
	gg->wabout.hide_on_close = 1;

	gg->winfo.hide_on_close = 1;
	gg->winfo.on_action = &gui_info_action;

	gg->wlog.hide_on_close = 1;

	gg->wconvert.hide_on_close = 1;
	gg->wconvert.on_action = &gui_cvt_action;

	gg->cmdtask.handler = &gui_task;
	ffui_dlg_multisel(&gg->dlg);
	ffui_tray_settooltipz(&gg->tray_icon, "fmedia");
	gg->vlist.colclick_id = SORT;

	gg->wmain.on_dropfiles = &gui_on_dropfiles;
	ffui_fdrop_accept(&gg->wmain, 1);

	fflk_unlock(&gg->lk);

	ffui_run();
	goto done;

err:
	gg->load_err = 1;
	fflk_unlock(&gg->lk);

done:
	ffui_dlg_destroy(&gg->dlg);
	ffui_wnd_destroy(&gg->wmain);
	ffui_uninit();
	return 0;
}

static const void* gui_iface(const char *name)
{
	if (!ffsz_cmp(name, "gui")) {
		if (NULL == (gg = ffmem_tcalloc1(ggui)))
			return NULL;

		return &fmed_gui;

	} else if (!ffsz_cmp(name, "log")) {
		return &gui_logger;
	}
	return NULL;
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
	}
	return 0;
}

static void gui_destroy(void)
{
	if (gg == NULL)
		return;
	ffui_wnd_close(&gg->wmain);
	ffthd_join(gg->th, -1, NULL);
	core->task(&gg->cmdtask, FMED_TASK_DEL);
	ffmem_safefree(gg->rec_dir);

	ffmem_safefree(gg->conv_dir);
	ffmem_safefree(gg->conv_name);
	ffmem_safefree(gg->conv_fmt);

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

static int gui_conf_conv_dir(ffparser_schem *ps, void *obj, char *val)
{
	if (NULL == (gg->conv_dir = ffenv_expand(NULL, 0, val)))
		return FFPARS_ESYS;
	ffmem_free(val);
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
	ffui_viewitem it = {0};
	const char *sval;
	char buf[1024];
	ffstr *tstr, artist = {0}, stitle;
	fmed_que_entry *plid;
	size_t n;
	ssize_t idx = -1;
	int64 total_samples;
	uint64 dur;
	gui_trk *g = ffmem_tcalloc1(gui_trk);
	if (g == NULL)
		return NULL;
	fflk_init(&g->lkcmds);
	g->trk = d->trk;
	g->task.handler = d->handler;
	g->task.param = d->trk;

	g->sample_rate = (int)fmed_getval("pcm_sample_rate");
	total_samples = fmed_getval("total_samples");
	g->total_time_sec = ffpcm_time(total_samples, g->sample_rate) / 1000;
	ffui_trk_setrange(&gg->tpos, g->total_time_sec);

	plid = (void*)fmed_getval("queue_item");
	if (plid == FMED_PNULL)
		return FMED_FILT_SKIP; //tracks being recorded are not started from "queue"
	if (-1 != (idx = ffui_view_search(&gg->vlist, (size_t)plid)))
		ffui_view_setindex(&it, idx);

	ffui_view_focus(&it, 1);
	ffui_view_set(&gg->vlist, H_IDX, &it);

	if (NULL != (tstr = gg->qu->meta_find(plid, FFSTR("artist"))))
		artist = *tstr;

	if (NULL == (tstr = gg->qu->meta_find(plid, FFSTR("title")))) {
		//use filename as a title
		ffpath_split2(plid->url.ptr, plid->url.len, NULL, &stitle);
		ffpath_splitname(stitle.ptr, stitle.len, &stitle, NULL);
	} else
		stitle = *tstr;

	n = ffs_fmt(buf, buf + sizeof(buf), "%S - %S - fmedia", &artist, &stitle);
	ffui_settext(&gg->wmain, buf, n);

	ffui_view_settextstr(&it, &artist);
	ffui_view_set(&gg->vlist, H_ART, &it);

	ffui_view_settextstr(&it, &stitle);
	ffui_view_set(&gg->vlist, H_TIT, &it);

	ffui_view_settextstr(&it, &plid->url);
	ffui_view_set(&gg->vlist, H_FN, &it);

	if (FMED_NULL != (dur = fmed_getval("track_duration")))
		dur /= 1000;
	else
		dur = g->total_time_sec;

	n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u"
		, dur / 60, dur % 60);
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&gg->vlist, H_DUR, &it);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %u bit, %s"
		, (int)(d->track->getval(d->trk, "bitrate") / 1000)
		, (FMED_PNULL != (sval = d->track->getvalstr(d->trk, "pcm_decoder"))) ? sval : ""
		, g->sample_rate
		, ffpcm_bits(d->track->getval(d->trk, "pcm_format"))
		, ffpcm_channelstr((int)d->track->getval(d->trk, "pcm_channels")));
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&gg->vlist, H_INF, &it);

	fflk_lock(&gg->lktrk);
	gg->curtrk = g;
	fflk_unlock(&gg->lktrk);

	gui_action(&gg->wmain, VOL);

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

	ffui_trk_set(&gg->tpos, playtime);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u"
		, playtime / 60, playtime % 60
		, g->total_time_sec / 60, g->total_time_sec % 60);
	ffui_stbar_settext(&gg->stbar, 0, buf, n);

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

	ffui_edit_addtext(&gg->tlog, buf, s - buf);

	if (!ffsz_cmp(level, "error"))
		ffui_show(&gg->wlog, 1);
}
