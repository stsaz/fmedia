/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>

#include <FF/gui/loader.h>
#include <FF/path.h>
#include <FFOS/dir.h>
#include <FFOS/process.h>


enum LIST_HDR {
	H_IDX,
	H_ART,
	H_TIT,
	H_DUR,
	H_INF,
	H_DATE,
	H_ALBUM,
	H_FN,
	_H_LAST,
};

static const char* const list_colname[] = {
	NULL,
	"artist",
	"title",
	"__dur",
	NULL,
	"date",
	"album",
	"__url",
};

static void gui_action(ffui_wnd *wnd, int id);
static void gui_media_opendlg(uint id);
static void gui_media_open(uint id);
static void gui_media_savelist(void);
static void gui_media_remove(void);
static void gui_list_rmdead(void);
static void gui_list_random(void);
static void sel_after_cur(void);
static void gui_tonxtlist(void);
static void gui_goto_show(void);
static void gui_go_set(void);
static void gui_vol(uint id);
static void gui_media_showdir(void);
static void gui_media_copyfn(void);
static void gui_media_fileop(uint cmd);
static void gui_showtextfile(uint cmd);
static void gui_on_dropfiles(ffui_wnd *wnd, ffui_fdrop *df);
static void gui_onclose(void);
static void gui_media_addurl(uint id);
static void gui_showrecdir(void);
static void gui_filt_show(void);
static void list_setdata(void);
static void fav_add(void);
static void fav_show(void);
static void list_cols_width_load();

enum {
	GUI_TRKINFO_WNDCAPTION = 1,
};


void wmain_init(void)
{
	ffui_show(&gg->wmain.wmain, 1);
	gg->wmain.wmain.top = 1;
	gg->wmain.wmain.on_action = &gui_action;
	gg->wmain.wmain.onclose_id = ONCLOSE;
	gg->wmain.wmain.manual_close = 1;
	gg->wmain.wmain.onminimize_id = (gg->minimize_to_tray) ? HIDE : 0;
	gui_newtab(0);
	ffui_tray_settooltipz(&gg->wmain.tray_icon, "fmedia");
	gg->wmain.vlist.colclick_id = SORT;
	gg->wmain.wmain.on_dropfiles = &gui_on_dropfiles;
	ffui_fdrop_accept(&gg->wmain.wmain, 1);

	list_cols_width_load();

	if (gg->list_random) {
		gg->list_random = 0;
		gui_list_random();
	}
	if (gg->sel_after_cur) {
		gg->sel_after_cur = 0;
		sel_after_cur();
	}

	ffui_icon_loadres(&gg->wmain.ico, L"#2", 0, 0);
	ffui_icon_loadres(&gg->wmain.ico_rec, L"#7", 0, 0);
}


static const struct cmd cmds[] = {
	{ PLAY,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ PAUSE,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ STOP,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ STOP_AFTER,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ NEXT,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ PREV,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },

	{ SEEK,	F1,	&gui_seek },
	{ FFWD,	F1,	&gui_seek },
	{ RWND,	F1,	&gui_seek },
	{ LEAP_FWD,	F1,	&gui_seek },
	{ LEAP_BACK,	F1,	&gui_seek },
	{ GOTO_SHOW,	F0,	&gui_goto_show },
	{ GOPOS,	F1,	&gui_seek },
	{ SETGOPOS,	F0,	&gui_go_set },

	{ VOL,	F1,	&gui_vol },
	{ VOLUP,	F1,	&gui_vol },
	{ VOLDOWN,	F1,	&gui_vol },
	{ VOLRESET,	F1,	&gui_vol },

	{ REC,	F1 | CMD_FCORE,	&gui_rec },
	{ REC_SETS,	F0,	&gui_rec_show },
	{ PLAYREC,	F1 | CMD_FCORE,	&gui_rec },
	{ MIXREC,	F1 | CMD_FCORE,	&gui_rec },
	{ SHOWRECS,	F0,	&gui_showrecdir },
	{ DEVLIST_SHOWREC,	F1,	&gui_dev_show },
	{ DEVLIST_SHOW,	F1,	&gui_dev_show },

	{ SHOWCONVERT,	F0,	&gui_showconvert },
	{ SETCONVPOS_SEEK,	F1,	&gui_setconvpos },
	{ SETCONVPOS_UNTIL,	F1,	&gui_setconvpos },

	{ OPEN,	F1,	&gui_media_opendlg },
	{ ADD,	F1,	&gui_media_opendlg },
	{ ADDURL,	F1,	&gui_media_addurl },
	{ QUE_NEW,	F0 | CMD_FCORE,	&gui_que_new },
	{ QUE_DEL,	F0 | CMD_FCORE,	&gui_que_del },
	{ QUE_SEL,	F0 | CMD_FCORE,	&gui_que_sel },
	{ SAVELIST,	F0,	&gui_media_savelist },
	{ REMOVE,	F0 | CMD_FCORE,	&gui_media_remove },
	{ RANDOM,	F0 | CMD_FCORE,	&gui_list_random },
	{ LIST_RMDEAD,	F0 | CMD_FCORE,	&gui_list_rmdead },
	{ CLEAR,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ TO_NXTLIST,	F0 | CMD_FCORE,	&gui_tonxtlist },
	{ SHOWDIR,	F0 | CMD_FCORE,	&gui_media_showdir },
	{ COPYFN,	F0 | CMD_FCORE,	&gui_media_copyfn },
	{ COPYFILE,	F1 | CMD_FCORE,	&gui_media_fileop },
	{ DELFILE,	F1 | CMD_FCORE,	&gui_media_fileop },
	{ SHOWINFO,	F0 | CMD_FCORE,	&gui_media_showinfo },
	{ SHOWPCM,	F0 | CMD_FCORE,	&gui_media_showpcm },
	{ FILTER_SHOW,	F0,	&gui_filt_show },
	{ SEL_AFTER_CUR,	F0,	&sel_after_cur },

	{ FAV_ADD,	F0 | CMD_FCORE,	&fav_add },
	{ FAV_SHOW,	F0 | CMD_FCORE,	&fav_show },

	{ CHECKUPDATE,	F0 | CMD_FCORE,	&gui_upd_check },

	{ CONF_EDIT,	F1,	&gui_showtextfile },
	{ USRCONF_EDIT,	F1,	&gui_showtextfile },
	{ FMEDGUI_EDIT,	F1,	&gui_showtextfile },
	{ THEMES_EDIT,	F1,	&gui_showtextfile },
	{ README_SHOW,	F1,	&gui_showtextfile },
	{ CHANGES_SHOW,	F1,	&gui_showtextfile },

	{ LIST_DISPINFO,	F0,	&list_setdata },
};

static const struct cmd cmd_open = { OPEN,	F1 | CMD_FCORE,	&gui_media_open };
const struct cmd cmd_add = { ADD,	F1 | CMD_FCORE,	&gui_media_open };
static const struct cmd cmd_quit = { QUIT,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op };
static const struct cmd cmd_savelist = { SAVELIST,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op };
static const struct cmd cmd_seek = { SEEK, F1 | CMD_FCORE | CMD_FUDATA, &gui_corecmd_op };
static const struct cmd cmd_vol = { VOL, F1 | CMD_FCORE | CMD_FUDATA, &gui_corecmd_op };

const struct cmd* getcmd(uint cmd, const struct cmd *cmds, uint n)
{
	size_t i, start = 0;
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

/** Provide GUI subsystem with the information it needs for a playlist item. */
static void list_setdata(void)
{
	char buf[256];
	LVITEM *it = gg->wmain.vlist.dispinfo_item;
	fmed_que_entry *ent;
	size_t n;
	ffstr *val = NULL, s;

	if (!(it->mask & LVIF_TEXT))
		return;

	ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, it->iItem);
	if (ent == NULL)
		return;

	switch (it->iSubItem) {

	case H_IDX: {
		fmed_que_entry *active_qent = NULL;
		fflk_lock(&gg->lktrk);
		if (gg->curtrk != NULL)
			active_qent = gg->curtrk->qent;
		fflk_unlock(&gg->lktrk);
		n = ffs_fmt(buf, buf + sizeof(buf), "%s%u"
			, (ent == active_qent) ? "> " : "", it->iItem + 1);
		ffstr_set(&s, buf, n);
		val = &s;
		break;
	}

	case H_ART:
		val = gg->qu->meta_find(ent, FFSTR("artist"));
		break;

	case H_TIT:
		if (NULL == (val = gg->qu->meta_find(ent, FFSTR("title")))) {
			//use filename as a title
			ffpath_split3(ent->url.ptr, ent->url.len, NULL, &s, NULL);
			val = &s;
		}
		break;

	case H_DUR:
		val = gg->qu->meta_find(ent, FFSTR("__dur"));
		if (val == NULL && ent->dur != 0) {
			uint sec = ent->dur / 1000;
			n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u", sec / 60, sec % 60);
			ffstr_set(&s, buf, n);
			val = &s;
		}
		break;

	case H_INF:
		val = gg->qu->meta_find(ent, FFSTR("__info"));
		break;

	case H_DATE:
		val = gg->qu->meta_find(ent, FFSTR("date"));
		break;

	case H_ALBUM:
		val = gg->qu->meta_find(ent, FFSTR("album"));
		break;

	case H_FN:
		val = &ent->url;
		break;
	}

	if (val != NULL)
		ffui_view_dispinfo_settext(it, val->ptr, val->len);

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);
}

static void gui_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, cmds, FFCNT(cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	if ((id & 0xff) == SETTHEME) {
		gui_theme_set(id >> 8);
		return;
	}

	switch (id) {
	case SEEKING:
		{
		uint pos = ffui_trk_val(&gg->wmain.tpos);
		char buf[64];
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Seek to %u:%02u"
			, pos / 60, pos % 60);
		gui_status(buf, n);
		}
		break;


	case SELALL:
		ffui_view_sel(&gg->wmain.vlist, -1);
		break;

	case SELINVERT:
		ffui_view_sel_invert(&gg->wmain.vlist);
		break;

	case SORT: {
		if (list_colname[gg->wmain.vlist.col] == NULL)
			break;
		ffui_viewcol vc = {};

		if (gg->sort_col >= 0 && gg->sort_col != gg->wmain.vlist.col) {
			ffui_viewcol_setsort(&vc, 0);
			ffui_view_setcol(&gg->wmain.vlist, gg->sort_col, &vc);
			gg->sort_reverse = 0;
		}

		gg->qu->cmdv(FMED_QUE_SORT, -1, list_colname[gg->wmain.vlist.col], gg->sort_reverse);

		ffui_viewcol_setsort(&vc, (gg->sort_reverse == 0) ? HDF_SORTUP : HDF_SORTDOWN);
		ffui_view_setcol(&gg->wmain.vlist, gg->wmain.vlist.col, &vc);
		gg->sort_col = gg->wmain.vlist.col;
		gg->sort_reverse = !gg->sort_reverse;
		ffui_ctl_invalidate(&gg->wmain.vlist);
		break;
	}

	case HIDE:
		if (!ffui_tray_visible(&gg->wmain.tray_icon)) {
			ffui_tray_seticon(&gg->wmain.tray_icon, &gg->wmain.ico);
			ffui_tray_show(&gg->wmain.tray_icon, 1);
		}
		ffui_show(&gg->wmain.wmain, 0);
		ffui_show(&gg->winfo.winfo, 0);
		ffui_show(&gg->wgoto.wgoto, 0);
		ffui_show(&gg->wconvert.wconvert, 0);
		ffui_show(&gg->wrec.wrec, 0);
		ffui_show(&gg->wdev.wnd, 0);
		ffui_show(&gg->wlog.wlog, 0);
		ffui_show(&gg->wuri.wuri, 0);
		ffui_show(&gg->wfilter.wnd, 0);
		gg->min_tray = 1;
		break;

	case SHOW:
		ffui_show(&gg->wmain.wmain, 1);
		ffui_wnd_setfront(&gg->wmain.wmain);
		if (!(gg->status_tray && gg->rec_trk != NULL))
			ffui_tray_show(&gg->wmain.tray_icon, 0);
		gg->min_tray = 0;
		break;

	case ABOUT:
		ffui_show(&gg->wabout.wabout, 1);
		break;

	case QUIT:
	case ONCLOSE:
		gui_corecmd_add(&cmd_quit, NULL);
		gui_onclose();
		break;
	}
}

static const char *const setts[] = {
	"wmain.placement", "wmain.tvol.value",
	"wconvert.position",
	"wrec.position",
	"winfo.position",
	"wlog.position",
	"wfilter.position",
};

static void gui_usrconf_write(void)
{
	char *fn;
	ffui_loaderw ldr = {0};

	if (NULL == (fn = gui_usrconf_filename()))
		return;

	ldr.getctl = &gui_getctl;
	ldr.udata = gg;
	ffui_ldr_setv(&ldr, setts, FFCNT(setts), 0);

	if (0 != ffui_ldr_write(&ldr, fn) && fferr_nofile(fferr_last())) {
		if (0 != ffdir_make_path(fn, 0) && fferr_last() != EEXIST) {
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

static void gui_onclose(void)
{
	gui_usrconf_write();
	usrconf_write();
}

static void gui_go_set(void)
{
	fflk_lock(&gg->lktrk);
	if (gg->curtrk != NULL) {
		gg->go_pos = gg->curtrk->lastpos;
	}
	fflk_unlock(&gg->lktrk);

	if (gg->go_pos == (uint)-1)
		return;

	char buf[255];
	size_t n = ffs_fmt(buf, buf + sizeof(buf), "Marker: %u:%02u"
		, gg->go_pos / 60, gg->go_pos % 60);
	gui_status(buf, n);
}

static void gui_goto_show(void)
{
	uint pos = ffui_trk_val(&gg->wmain.tpos);
	ffarr s = {0};
	ffstr_catfmt(&s, "%02u:%02u", pos / 60, pos % 60);
	ffui_settextstr(&gg->wgoto.etime, &s);
	ffarr_free(&s);
	ffui_edit_selall(&gg->wgoto.etime);
	ffui_setfocus(&gg->wgoto.etime);
	ffui_show(&gg->wgoto.wgoto, 1);
}

/*
Note: if Left/Right key is pressed while trackbar is focused, SEEK command will be received after RWND/FFWD. */
void gui_seek(uint cmd)
{
	uint pos;
	int delta;
	if (gg->curtrk == NULL)
		return;

	switch (cmd) {
	default:
	case GOTO:
		pos = ffui_trk_val(&gg->wmain.tpos);
		break;

	case FFWD:
		delta = gg->seek_step_delta;
		goto use_delta;

	case RWND:
		delta = -(int)gg->seek_step_delta;
		goto use_delta;

	case LEAP_FWD:
		delta = gg->seek_leap_delta;
		goto use_delta;

	case LEAP_BACK:
		delta = -(int)gg->seek_leap_delta;
		goto use_delta;

use_delta:
		pos = ffui_trk_val(&gg->wmain.tpos);
		pos = ffmax((int)pos + delta, 0);
		ffui_trk_set(&gg->wmain.tpos, pos);
		break;

	case GOPOS:
		if (gg->go_pos == (uint)-1)
			return;
		pos = gg->go_pos;
		ffui_trk_set(&gg->wmain.tpos, pos);
		break;
	}

	gui_corecmd_add(&cmd_seek, (void*)(size_t)pos);
}

/** Convert volume trackbar position to dB value. */
double gui_getvol(void)
{
	double db;
	uint pos = ffui_trk_val(&gg->wmain.tvol);
	if (pos <= 100)
		db = ffpcm_vol2db(pos, 48);
	else
		db = ffpcm_vol2db_inc(pos - 100, 25, 6);
	return db;
}

static void gui_vol(uint id)
{
	char buf[64];
	double db;
	size_t n;

	switch (id) {
	case VOLUP:
		ffui_trk_move(&gg->wmain.tvol, FFUI_TRK_PGUP);
		break;

	case VOLDOWN:
		ffui_trk_move(&gg->wmain.tvol, FFUI_TRK_PGDN);
		break;

	case VOLRESET:
		ffui_trk_set(&gg->wmain.tvol, 100);
		break;
	}

	db = gui_getvol();
	n = ffs_fmt(buf, buf + sizeof(buf), "Volume: %.02FdB", db);
	gui_status(buf, n);
	gg->vol = db * 100;

	gui_corecmd_add(&cmd_vol, NULL);
}

static void gui_media_showdir(void)
{
	const fmed_que_entry *ent;
	int focused;
	if (-1 == (focused = ffui_view_focused(&gg->wmain.vlist)))
		return;
	if (NULL == (ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, focused)))
		return;

	ffui_openfolder((const char *const *)&ent->url.ptr, 1);
}

static void gui_showrecdir(void)
{
	char *p, *exp;
	ffstr dir;
	ffpath_split2(gg->rec_sets.output, ffsz_len(gg->rec_sets.output), &dir, NULL);
	if (NULL == (p = ffsz_alcopy(dir.ptr, dir.len)))
		return;
	if (NULL == (exp = core->env_expand(NULL, 0, p)))
		goto done;
	ffui_openfolder((const char *const *)&exp, 0);

done:
	ffmem_safefree(p);
	ffmem_safefree(exp);
}

/** Copy to clipboard filenames of selected items:
/path/file1 CRLF
/path/file2 */
static void gui_media_copyfn(void)
{
	int i = -1;
	fmed_que_entry *ent;
	ffarr buf = {0};

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

		if (0 == ffstr_catfmt(&buf, "%S" FF_NEWLN, &ent->url))
			goto done;
	}

	if (buf.len == 0)
		goto done;

	ffui_clipbd_set(buf.ptr, buf.len - FFSLEN(FF_NEWLN));

done:
	ffarr_free(&buf);
}

/** Update number of total list items and set to redraw the next items. */
static void list_update(uint idx, int delta)
{
	uint n = ffui_view_nitems(&gg->wmain.vlist);
	ffui_view_setcount(&gg->wmain.vlist, n + delta);
	ffui_view_redraw(&gg->wmain.vlist, idx, idx + 100);
}

struct ent_idx {
	uint idx;
	fmed_que_entry *ent;
};

static void gui_media_fileop(uint cmd)
{
	int i = -1;
	fmed_que_entry *ent;
	struct ent_idx *ei;
	ffarr buf = {0}; //char*[]
	ffarr ents = {0}; //ent_idx[]
	char st[255];
	size_t n;
	char **pitem;

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

		if (NULL == (pitem = ffarr_pushgrowT(&buf, 16, char*)))
			goto done;
		*pitem = ent->url.ptr;

		switch (cmd) {
		case DELFILE:
			if (NULL == (ei = ffarr_pushgrowT(&ents, 16, struct ent_idx)))
				goto done;
			ei->ent = ent;
			ei->idx = i;
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
			FFARR_WALKT(&ents, ei, struct ent_idx) {
				gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ei->ent);
			}
			ffui_view_unselall(&gg->wmain.vlist);
			ei = (void*)ents.ptr;
			list_update(ei->idx, -(int)buf.len);
			n = ffs_fmt(st, st + sizeof(st), "Deleted %L files", buf.len);
			gui_status(st, n);
		}
		break;
	}

done:
	ffarr_free(&buf);
	ffarr_free(&ents);
}

static void gui_showtextfile(uint cmd)
{
	char *notepad, *fn;

	if (NULL == (notepad = core->env_expand(NULL, 0, "%SYSTEMROOT%\\system32\\notepad.exe")))
		return;

	switch (cmd) {
	case CONF_EDIT:
		fn = core->getpath(FFSTR(FMED_GLOBCONF));
		break;
	case USRCONF_EDIT:
		fn = gui_userpath(FMED_USERCONF);
		break;
	case FMEDGUI_EDIT:
		fn = core->getpath(FFSTR("fmedia.gui"));
		break;
	case THEMES_EDIT:
		fn = core->getpath(FFSTR("theme.conf"));
		break;
	case README_SHOW:
		fn = core->getpath(FFSTR("README.txt"));
		break;
	case CHANGES_SHOW:
		fn = core->getpath(FFSTR("CHANGES.txt"));
		break;
	default:
		goto end;
	}

	if (fn == NULL)
		goto end;

	const char *args[3] = {
		notepad, fn, NULL
	};
	fffd ps;
	if (FF_BADFD != (ps = ffps_exec(notepad, args, NULL)))
		ffps_close(ps);

	ffmem_free(fn);
end:
	ffmem_free(notepad);
}

static void gui_trk_setinfo2(fmed_que_entry *ent, uint flags)
{
	uint n;
	char buf[255];
	ffstr artist = {0}, title = {0}, *val;

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("artist"))))
		artist = *val;

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("title"))))
		title = *val;
	else {
		//use filename as a title
		ffpath_split3(ent->url.ptr, ent->url.len, NULL, &title, NULL);
	}

	if (flags & GUI_TRKINFO_WNDCAPTION) {
		n = ffs_fmt(buf, buf + sizeof(buf), "%S - %S - fmedia", &artist, &title);
		ffui_settext(&gg->wmain.wmain, buf, n);
	}
}

void gui_media_added(fmed_que_entry *ent)
{
	uint idx = gg->qu->cmdv(FMED_QUE_ID, ent);
	list_update(idx, 1);
}

static void gui_media_opendlg(uint id)
{
	const char *fn, **ps;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_INPUT);
	if (NULL == (fn = ffui_dlg_open(&gg->dlg, &gg->wmain.wmain)))
		return;

	do {
		if (NULL == (ps = ffarr_pushgrowT(&gg->filenames, 16, const char*)))
			goto err;
		if (NULL == (*ps = ffsz_alcopyz(fn))) {
			gg->filenames.len--;
			goto err;
		}
	} while (NULL != (fn = ffui_dlg_nextname(&gg->dlg)));

	if (id == OPEN)
		gui_corecmd_add(&cmd_open, NULL);
	else
		gui_corecmd_add(&cmd_add, NULL);

	return;

err:
	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
}

static void gui_media_open(uint id)
{
	const char **pfn;

	ffui_redraw(&gg->wmain.vlist, 0);

	if (id == OPEN)
		gui_corecmd_op(CLEAR, NULL);

	FFARR_WALKT(&gg->filenames, pfn, const char*) {
		gui_media_add1(*pfn);
	}

	ffui_redraw(&gg->wmain.vlist, 1);

	if (id == OPEN)
		gui_corecmd_op(NEXT, NULL);

	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
}

static void gui_media_addurl(uint id)
{
	ffui_edit_selall(&gg->wuri.turi);
	ffui_setfocus(&gg->wuri.turi);
	ffui_show(&gg->wuri.wuri, 1);
}

void gui_media_removed(uint i)
{
	list_update(i, -1);
}

int gui_newtab(uint flags)
{
	char buf[32];
	static uint tabs;
	ffui_tabitem it = {0};

	if (flags & GUI_TAB_CONVERT) {
		ffui_tab_settextz(&it, "Converting...");
	} else if (flags & GUI_TAB_FAV) {
		ffui_tab_settextz(&it, "Favorites");
	} else {
		int n = ffs_fmt(buf, buf + sizeof(buf), "Playlist %u", ++tabs);
		ffui_tab_settext(&it, buf, n);
	}
	int itab = ffui_tab_append(&gg->wmain.tabs, &it);
	if (!(flags & GUI_TAB_NOSEL))
		ffui_tab_setactive(&gg->wmain.tabs, itab);
	return itab;
}

void gui_que_new(void)
{
	int itab = gui_newtab(0);
	gg->qu->cmd(FMED_QUE_NEW, NULL);
	ffui_view_clear(&gg->wmain.vlist);
	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)itab);
}

void gui_showque(uint i)
{
	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)i);
	uint n = gg->qu->cmdv(FMED_QUE_COUNT);
	ffui_view_setcount(&gg->wmain.vlist, n);
	ffui_view_redraw(&gg->wmain.vlist, 0, 100);
}

void gui_que_del(void)
{
	int sel = ffui_tab_active(&gg->wmain.tabs);
	if (sel == -1)
		return;

	if (sel == gg->itab_convert)
		gg->itab_convert = -1;
	else if (gg->itab_convert > sel)
		gg->itab_convert--;

	if (sel == gg->fav_pl) {
		fav_save();
		gg->fav_pl = -1;
	} else if (gg->fav_pl > sel)
		gg->fav_pl--;

	ffui_tab_del(&gg->wmain.tabs, sel);
	ffbool last = (0 == ffui_tab_count(&gg->wmain.tabs));
	sel = ffmax(sel - 1, 0);
	ffui_tab_setactive(&gg->wmain.tabs, sel);

	gg->qu->cmd(FMED_QUE_DEL, NULL);
	if (!last)
		gui_showque(sel);
	else
		gui_que_new();
}

void gui_que_sel(void)
{
	int sel = ffui_tab_active(&gg->wmain.tabs);
	gui_showque(sel);
}

static void gui_media_savelist(void)
{
	char *fn;
	ffstr name;
	ffstr_setz(&name, "Playlist");
	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_PLAYLISTS);
	gg->dlg.of.lpstrDefExt = L""; //the first extension from the current filter will be appended to filename
	fn = ffui_dlg_save(&gg->dlg, &gg->wmain.wmain, name.ptr, name.len);
	gg->dlg.of.lpstrDefExt = NULL;
	if (fn == NULL)
		return;

	char *list_fn;
	if (NULL == (list_fn = ffsz_alcopyz(fn)))
		return;
	gui_corecmd_add(&cmd_savelist, list_fn);
}

static void gui_media_remove(void)
{
	int i = -1, n = 0, first = -1;
	void *id;

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		if (first < 0)
			first = i;
		id = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i - n);
		gg->qu->cmd2(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, id, 0);
		n++;
	}
	ffui_view_unselall(&gg->wmain.vlist);
	list_update(first, -n);
}

static void gui_list_rmdead(void)
{
	ffui_redraw(&gg->wmain.vlist, 0);
	gg->qu->cmd(FMED_QUE_RMDEAD, NULL);
	ffui_redraw(&gg->wmain.vlist, 1);
}

/** Set core module's property and check/uncheck menu item. */
static void gui_list_random(void)
{
	gg->list_random = !gg->list_random;
	core->props->list_random = gg->list_random;
	ffui_menuitem mi = {};
	if (gg->list_random)
		ffui_menu_addstate(&mi, FFUI_MENU_CHECKED);
	else
		ffui_menu_clearstate(&mi, FFUI_MENU_CHECKED);
	ffui_menu_set_byid(&gg->mlist, RANDOM, &mi);
}

static void sel_after_cur(void)
{
	gg->sel_after_cur = !gg->sel_after_cur;
	ffui_menuitem mi = {};
	if (gg->sel_after_cur)
		ffui_menu_addstate(&mi, FFUI_MENU_CHECKED);
	else
		ffui_menu_clearstate(&mi, FFUI_MENU_CHECKED);
	ffui_menu_set_byid(&gg->mlist, SEL_AFTER_CUR, &mi);
}

/* Favorites playlist.
This is a persistent song list with automatic load and save.
Playlist contents are stored on disk on list close and on application exit.
*/

/** Create playlist and load contents. */
static void fav_prep(uint flags)
{
	if (gg->fav_pl != -1)
		return;

	gg->fav_pl = ffui_tab_count(&gg->wmain.tabs);
	gui_newtab(GUI_TAB_FAV | flags);
	gg->qu->cmd(FMED_QUE_NEW, NULL);

	char *fn = ffsz_alfmt("%s%s", core->props->user_path, GUI_FAV_NAME);
	gui_media_add2(fn, gg->fav_pl, ADDF_CHECKTYPE);
	ffmem_free(fn);
}

/** Save playlist to disk. */
void fav_save(void)
{
	char *fn = ffsz_alfmt("%s%s", core->props->user_path, GUI_FAV_NAME);
	gg->qu->fmed_queue_save(gg->fav_pl, fn);
	ffmem_free(fn);
}

/** Show playlist. */
static void fav_show(void)
{
	fav_prep(0);
	gui_showque(gg->fav_pl);
}

/** Add selected items to playlist.
Load playlist from file if it isn't loaded.
Create playlist if it doesn't exist. */
static void fav_add(void)
{
	int i = -1, sel;
	fmed_que_entry e = {}, *ent;

	if (0 == ffui_view_selcount(&gg->wmain.vlist))
		return;
	if (-1 == (sel = ffui_tab_active(&gg->wmain.tabs)))
		return;

	fav_prep(GUI_TAB_NOSEL);

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(sel, i);
		e.url = ent->url;
		e.from = ent->from;
		e.to = ent->to;

		gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, gg->fav_pl, &e);
	}
}

static void gui_tonxtlist(void)
{
	int i = -1, sel;
	fmed_que_entry e = {0}, *ent;

	if (0 == ffui_view_selcount(&gg->wmain.vlist))
		return;

	if (-1 == (sel = ffui_tab_active(&gg->wmain.tabs)))
		return;

	if (sel + 1 == ffui_tab_count(&gg->wmain.tabs)) {
		gui_newtab(GUI_TAB_NOSEL);
		gg->qu->cmd(FMED_QUE_NEW, NULL);
	}

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(sel, i);
		e.url = ent->url;
		e.from = ent->from;
		e.to = ent->to;

		gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, sel + 1, &e);
	}
}

void gui_status(const char *s, size_t len)
{
	ffui_stbar_settext(&gg->wmain.stbar, 1, s, len);
}

void gui_clear(void)
{
	ffui_settextz(&gg->wmain.wmain, "fmedia");
	ffui_trk_set(&gg->wmain.tpos, 0);
	ffui_trk_setrange(&gg->wmain.tpos, 0);
	ffui_settext(&gg->wmain.lpos, NULL, 0);
	gui_status("", 0);
}

static void gui_on_dropfiles(ffui_wnd *wnd, ffui_fdrop *df)
{
	const char *fn, **ps;

	while (NULL != (fn = ffui_fdrop_next(df))) {

		if (NULL == (ps = ffarr_pushgrowT(&gg->filenames, 16, const char*)))
			goto err;
		if (NULL == (*ps = ffsz_alcopyz(fn))) {
			gg->filenames.len--;
			goto err;
		}
	}

	gui_corecmd_add(&cmd_add, NULL);
	return;

err:
	FFARR_FREE_ALL_PTR(&gg->filenames, ffmem_free, char*);
}

static void gui_filt_show(void)
{
	ffui_edit_selall(&gg->wfilter.ttext);
	ffui_setfocus(&gg->wfilter.ttext);
	ffui_show(&gg->wfilter.wnd, 1);
}

void gui_filter(const ffstr *text, uint flags)
{
	fmed_que_entry *e = NULL;
	ffstr *meta, name;
	uint nfilt = 0, nall = 0, inc;

	if (!gg->list_filter && text->len < 2)
		return; //too small filter text

	gg->qu->cmdv(FMED_QUE_NEW_FILTERED);

	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_clear(&gg->wmain.vlist);

	for (;;) {

		if (0 == gg->qu->cmd2(FMED_QUE_LIST_NOFILTER, &e, 0))
			break;
		inc = 0;

		if (text->len == 0)
			inc = 1;

		else if ((flags & GUI_FILT_URL) && -1 != ffstr_ifind(&e->url, text->ptr, text->len))
			inc = 1;

		else if (flags & GUI_FILT_META) {

			for (uint i = 0;  NULL != (meta = gg->qu->meta(e, i, &name, 0));  i++) {
				if (meta == FMED_QUE_SKIP)
					continue;
				if (-1 != ffstr_ifind(meta, text->ptr, text->len)) {
					inc = 1;
					break;
				}
			}
		}

		if (inc) {
			gg->qu->cmdv(FMED_QUE_ADD_FILTERED, e);
			gui_media_added(e);
			nfilt++;
		}

		nall++;
	}

	ffui_redraw(&gg->wmain.vlist, 1);

	gg->list_filter = (text->len != 0);
	if (text->len != 0) {
		char buf[128];
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Filter: %u (%u)", nfilt, nall);
		gui_status(buf, n);
	} else {
		gg->qu->cmdv(FMED_QUE_DEL_FILTERED);
		gui_status(NULL, 0);
	}
}


/** Update window caption and playlist item with a new meta. */
int gui_setmeta(gui_trk *g, fmed_que_entry *plid)
{
	ssize_t idx;
	if (-1 != (idx = gg->qu->cmdv(FMED_QUE_ID, plid))) {
		fmed_que_entry *e2 = (void*)gg->qu->fmed_queue_item(-1, idx);
		if (e2 != plid)
			idx = -1; //'plid' track isn't from the current playlist
	}

	if (idx != -1) {
		if (g == NULL) {
			ffui_view_redraw(&gg->wmain.vlist, idx, idx);
			return idx;
		}

		plid->dur = g->total_time_sec * 1000;
		ffui_view_redraw(&gg->wmain.vlist, idx, idx);
	}

	if (g == NULL)
		return -1;

	gui_trk_setinfo2(plid, GUI_TRKINFO_WNDCAPTION);
	return idx;
}

/** Update all properties of the playlist item with a new info. */
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid)
{
	char buf[1024];
	size_t n;

	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %s, %s"
		, (d->audio.bitrate + 500) / 1000
		, d->audio.decoder
		, g->sample_rate
		, ffpcm_fmtstr(d->audio.fmt.format)
		, ffpcm_channelstr(d->audio.fmt.channels));
	gg->qu->meta_set(g->qent, FFSTR("__info"), buf, n, FMED_QUE_PRIV | FMED_QUE_OVWRITE);

	if (-1 == gui_setmeta(g, plid))
		goto done;

	if (gg->sel_after_cur) {
		ffui_view_unselall(&gg->wmain.vlist);
		int idx = gg->qu->cmdv(FMED_QUE_ID, plid);
		ffui_view_sel(&gg->wmain.vlist, idx);
	}

done:
	ffui_trk_setrange(&gg->wmain.tpos, g->total_time_sec);
}

void wmain_update(uint playtime, uint time_total)
{
	ffui_trk_set(&gg->wmain.tpos, playtime);

	char buf[256];
	size_t n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u"
		, playtime / 60, playtime % 60
		, time_total / 60, time_total % 60);
	ffui_settext(&gg->wmain.lpos, buf, n);
}

/** Show progress of conversion track. */
void gui_conv_progress(gui_trk *g)
{
	char buf[255];
	uint playtime = (uint)(ffpcm_time(g->d->audio.pos, g->sample_rate) / 1000);
	if (playtime == g->lastpos && !(g->d->flags & FMED_FLAST))
		return;
	g->lastpos = playtime;

	fmed_que_entry *plid;
	plid = (void*)g->d->track->getval(g->d->trk, "queue_item");

	ssize_t idx;
	if (-1 == (idx = gg->qu->cmdv(FMED_QUE_ID, plid))) {
		FF_ASSERT(0);
		return;
	}

	ffstr val;
	if (g->d->flags & FMED_FLAST)
		ffstr_setz(&val, "Done");
	else {
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u"
			, playtime / 60, playtime % 60
			, g->total_time_sec / 60, g->total_time_sec % 60);
		ffstr_set(&val, buf, n);
	}
	gg->qu->meta_set(plid, FFSTR("__dur"), val.ptr, val.len, FMED_QUE_PRIV | FMED_QUE_OVWRITE);
	ffui_view_redraw(&gg->wmain.vlist, idx, idx);
}

/** Load widths of list's columns. */
static void list_cols_width_load()
{
	ffui_viewcol vc;
	ffui_viewcol_reset(&vc);
	for (uint i = 0;  i != _H_LAST;  i++) {
		FF_ASSERT(i != FFCNT(gg->list_col_width));
		if (gg->list_col_width[i] == 0)
			continue;

		ffui_viewcol_setwidth(&vc, gg->list_col_width[i]);
		ffui_view_setcol(&gg->wmain.vlist, i, &vc);
	}
}

/** Write widths of list's columns to config. */
void wmain_list_cols_width_write(ffconfw *conf)
{
	ffui_viewcol vc;
	for (uint i = 0;  i != _H_LAST;  i++) {
		ffui_viewcol_reset(&vc);
		ffui_viewcol_setwidth(&vc, 0);
		ffui_view_col(&gg->wmain.vlist, i, &vc);
		ffconf_writeint(conf, ffui_viewcol_width(&vc), 0, FFCONF_TVAL);
	}
}

void wmain_rec_started()
{
	ffui_stbar_settextz(&gg->wmain.stbar, 0, "Recording...");
	if (gg->status_tray && !ffui_tray_visible(&gg->wmain.tray_icon)) {
		ffui_tray_seticon(&gg->wmain.tray_icon, &gg->wmain.ico_rec);
		ffui_tray_show(&gg->wmain.tray_icon, 1);
	}
}

void wmain_rec_stopped()
{
	ffui_stbar_settextz(&gg->wmain.stbar, 0, "");
	if (gg->status_tray && !gg->min_tray)
		ffui_tray_show(&gg->wmain.tray_icon, 0);
}
