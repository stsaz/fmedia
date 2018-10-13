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
};

static void gui_action(ffui_wnd *wnd, int id);
static void gui_list_add(ffui_viewitem *it, size_t par);
static int __stdcall gui_list_sortfunc(LPARAM p1, LPARAM p2, LPARAM udata);
static void gui_media_opendlg(uint id);
static void gui_media_open(uint id);
static void gui_media_savelist(void);
static void gui_plist_recount(uint from);
static void gui_media_remove(void);
static void gui_list_rmdead(void);
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

enum {
	GUI_TRKINFO_WNDCAPTION = 1,
	GUI_TRKINFO_PLAYING = 2,
	GUI_TRKINFO_STOPPED = 4,
	GUI_TRKINFO_DUR = 8,
};
static void gui_trk_setinfo(int idx, fmed_que_entry *ent, uint sec, uint flags);


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

	char *fn;
	ffui_icon_loadres(&gg->wmain.ico, L"#2", 0, 0);

	if (NULL == (fn = core->getpath(FFSTR("fmedia-rec.ico"))))
		return;
	ffui_icon_load(&gg->wmain.ico_rec, fn, 0, FFUI_ICON_SMALL);
	ffmem_free(fn);
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
	{ DEVLIST_SHOWREC,	F0,	&gui_dev_show },

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

	{ CHECKUPDATE,	F0 | CMD_FCORE,	&gui_upd_check },

	{ CONF_EDIT,	F1,	&gui_showtextfile },
	{ USRCONF_EDIT,	F1,	&gui_showtextfile },
	{ FMEDGUI_EDIT,	F1,	&gui_showtextfile },
	{ README_SHOW,	F1,	&gui_showtextfile },
	{ CHANGES_SHOW,	F1,	&gui_showtextfile },
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

	case SORT:
		if (gg->wmain.vlist.col == H_TIT || gg->wmain.vlist.col == H_ART || gg->wmain.vlist.col == H_FN)
			ffui_view_sort(&gg->wmain.vlist, &gui_list_sortfunc, gg->wmain.vlist.col);
		break;

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

static const char *const setts[] = {
	"wmain.placement", "wmain.tvol.value",
	"wconvert.position", "wconvert.eout.text",
	"wrec.position",
	"winfo.position",
	"wlog.position",
	"wfilter.position",
};

static void gui_onclose(void)
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
			ffui_redraw(&gg->wmain.vlist, 0);
			FFARR_WALKT(&ents, ei, struct ent_idx) {
				gg->qu->cmd(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, ei->ent);
			}
			FFARR_RWALKT(&ents, ei, struct ent_idx) {
				ffui_view_rm(&gg->wmain.vlist, ei->idx);
			}
			if (ents.len != 0) {
				ei = ffarr_itemT(&ents, 0, struct ent_idx);
				gui_plist_recount(ei->idx);
			}
			ffui_redraw(&gg->wmain.vlist, 1);
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
		fn = core->env_expand(NULL, 0, "%APPDATA%/fmedia/fmedia-user.conf");
		break;
	case FMEDGUI_EDIT:
		fn = core->getpath(FFSTR("fmedia.gui"));
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

	const char *args[2];
	args[0] = fn;
	args[1] = NULL;
	fffd ps;
	if (FF_BADFD != (ps = ffps_exec(notepad, args, NULL)))
		ffps_close(ps);

	ffmem_free(fn);
end:
	ffmem_free(notepad);
}

static void gui_trk_setinfo(int idx, fmed_que_entry *ent, uint sec, uint flags)
{
	uint n;
	char buf[255];
	ffstr artist = {0}, title = {0}, *val;
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffui_view_setindex(&it, idx);

	if (flags & GUI_TRKINFO_STOPPED) {
		ffui_view_gettext(&it);
		ffui_view_get(&gg->wmain.vlist, H_IDX, &it);
		ffsyschar *s = ffui_view_textq(&it);
		if (s[0] == '>') {
			ffui_view_settext_q(&it, s + FFSLEN("> "));
			ffui_view_set(&gg->wmain.vlist, H_IDX, &it);
		}
		return;

	} else if (flags & GUI_TRKINFO_PLAYING) {
		ffui_view_gettext(&it);
		ffui_view_get(&gg->wmain.vlist, H_IDX, &it);
		n = ffs_fmt(buf, buf + sizeof(buf), "> %q", ffui_view_textq(&it));
		ffui_view_settext(&it, buf, n);
		ffui_view_set(&gg->wmain.vlist, H_IDX, &it);
	}

	ffui_view_settextstr(&it, &ent->url);
	ffui_view_set(&gg->wmain.vlist, H_FN, &it);

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("artist"))))
		artist = *val;

	if (NULL == (val = gg->qu->meta_find(ent, FFSTR("title")))) {
		//use filename as a title
		ffpath_split3(ent->url.ptr, ent->url.len, NULL, &title, NULL);
	} else
		title = *val;

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("__info")))) {
		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->wmain.vlist, H_INF, &it);
	}

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("date")))) {
		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->wmain.vlist, H_DATE, &it);
	}

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("album")))) {
		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->wmain.vlist, H_ALBUM, &it);
	}


	ffui_view_settextstr(&it, &artist);
	ffui_view_set(&gg->wmain.vlist, H_ART, &it);

	ffui_view_settextstr(&it, &title);
	ffui_view_set(&gg->wmain.vlist, H_TIT, &it);

	if (flags & GUI_TRKINFO_DUR) {
		n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u", sec / 60, sec % 60);
		ffui_view_settext(&it, buf, n);
		ffui_view_set(&gg->wmain.vlist, H_DUR, &it);
	}
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

void gui_media_added2(fmed_que_entry *ent, uint flags, int idx)
{
	char buf[255];
	ffui_viewitem it;
	ffmem_tzero(&it);

	if (idx == -1) {
		gui_list_add(&it, 0);
		idx = it.item.iItem;
	} else {
		size_t n = ffs_fromint(idx + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		ffui_view_ins(&gg->wmain.vlist, idx, &it);
	}

	if (ent->dur != 0)
		flags |= GUI_TRKINFO_DUR;
	gui_trk_setinfo(idx, ent, ent->dur / 1000, flags);
}

void gui_media_added(fmed_que_entry *ent, uint flags)
{
	gui_media_added2(ent, flags, -1);
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

static void gui_plist_recount(uint from)
{
	ffui_viewitem it;
	char buf[FFINT_MAXCHARS];
	size_t n;
	uint i;
	for (i = from;  ;  i++) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		n = ffs_fromint(i + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		if (0 != ffui_view_set(&gg->wmain.vlist, H_IDX, &it))
			break;
	}
}

void gui_media_removed(uint i)
{
	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_rm(&gg->wmain.vlist, i);
	gui_plist_recount(i);
	ffui_redraw(&gg->wmain.vlist, 1);
}

int gui_newtab(uint flags)
{
	char buf[32];
	static uint tabs;
	ffui_tabitem it = {0};

	if (flags & GUI_TAB_CONVERT) {
		ffui_tab_settextz(&it, "Converting...");
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

	fmed_que_entry *active_qent = NULL;
	if (gg->curtrk != NULL)
		active_qent = (void*)gg->track->getval(gg->curtrk->trk, "queue_item");

	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_clear(&gg->wmain.vlist);
	fmed_que_entry *e = NULL;
	for (;;) {
		if (0 == gg->qu->cmd2(FMED_QUE_LIST, &e, 0))
			break;
		gui_media_added(e, (e == active_qent) ? GUI_TRKINFO_PLAYING : 0);
	}
	ffui_redraw(&gg->wmain.vlist, 1);
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
	int i, first;
	void *id;

	ffui_redraw(&gg->wmain.vlist, 0);

	first = ffui_view_selnext(&gg->wmain.vlist, -1);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, -1))) {
		id = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);
		gg->qu->cmd2(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, id, 0);
		ffui_view_rm(&gg->wmain.vlist, i);
	}

	if (first != -1)
		gui_plist_recount(first);
	ffui_redraw(&gg->wmain.vlist, 1);
}

static void gui_list_rmdead(void)
{
	ffui_redraw(&gg->wmain.vlist, 0);
	gg->qu->cmd(FMED_QUE_RMDEAD, NULL);
	gui_plist_recount(0);
	ffui_redraw(&gg->wmain.vlist, 1);
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

	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)sel + 1);

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ent = (fmed_que_entry*)gg->qu->fmed_queue_item(sel, i);
		e.url = ent->url;
		e.from = ent->from;
		e.to = ent->to;

		gg->qu->cmd(FMED_QUE_ADD | FMED_QUE_NO_ONCHANGE, &e);
	}

	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)sel);
}

static void gui_list_add(ffui_viewitem *it, size_t par)
{
	char buf[FFINT_MAXCHARS];
	size_t n = ffs_fromint(ffui_view_nitems(&gg->wmain.vlist) + 1, buf, sizeof(buf), 0);
	ffui_view_settext(it, buf, n);
	ffui_view_append(&gg->wmain.vlist, it);
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
	fmed_que_entry *active_qent = NULL, *e = NULL;
	ffstr *meta, name;
	uint nfilt = 0, nall = 0, inc = 0;

	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_clear(&gg->wmain.vlist);

	if (gg->curtrk != NULL)
		active_qent = (void*)gg->track->getval(gg->curtrk->trk, "queue_item");

	for (;;) {

		if (0 == gg->qu->cmd2(FMED_QUE_LIST, &e, 0))
			break;

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
			inc = 0;
			gui_media_added(e, (e == active_qent) ? GUI_TRKINFO_PLAYING : 0);
			nfilt++;
		}

		nall++;
	}

	ffui_redraw(&gg->wmain.vlist, 1);

	if (text->len != 0) {
		char buf[128];
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Filter: %u (%u)", nfilt, nall);
		gui_status(buf, n);
	} else
		gui_status(NULL, 0);
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
			gui_trk_setinfo(idx, plid, 0, GUI_TRKINFO_STOPPED);
			return idx;
		}

		gui_trk_setinfo(idx, plid, g->total_time_sec, GUI_TRKINFO_PLAYING | GUI_TRKINFO_DUR);
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
	gg->qu->meta_set(g->qent, FFSTR("__info"), buf, n, FMED_QUE_PRIV);

	if (-1 == gui_setmeta(g, plid))
		goto done;

done:
	ffui_trk_setrange(&gg->wmain.tpos, g->total_time_sec);
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

	ffui_viewitem it = {0};
	ffui_view_setindex(&it, idx);
	if (g->d->flags & FMED_FLAST)
		ffui_view_settextz(&it, "Done");
	else {
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u"
			, playtime / 60, playtime % 60
			, g->total_time_sec / 60, g->total_time_sec % 60);
		ffui_view_settext(&it, buf, n);
	}
	ffui_view_set(&gg->wmain.vlist, H_DUR, &it);
}
