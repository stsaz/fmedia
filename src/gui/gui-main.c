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
	H_FN,
};

static void gui_action(ffui_wnd *wnd, int id);
static void gui_list_add(ffui_viewitem *it, size_t par);
static int __stdcall gui_list_sortfunc(LPARAM p1, LPARAM p2, LPARAM udata);
static void gui_media_open(uint id);
static void gui_media_savelist(void);
static void gui_plist_recount(uint from);
static void gui_media_remove(void);
static fmed_que_entry* gui_list_getent(void);
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
	gui_newtab(0);
	ffui_tray_settooltipz(&gg->wmain.tray_icon, "fmedia");
	gg->wmain.vlist.colclick_id = SORT;
	gg->wmain.wmain.on_dropfiles = &gui_on_dropfiles;
	ffui_fdrop_accept(&gg->wmain.wmain, 1);
}


static const struct cmd cmds[] = {
	{ PAUSE,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ STOP,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ STOP_AFTER,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ NEXT,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ PREV,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },

	{ SEEK,	F1,	&gui_seek },
	{ FFWD,	F1,	&gui_seek },
	{ RWND,	F1,	&gui_seek },
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

	{ SHOWCONVERT,	F0,	&gui_showconvert },
	{ SETCONVPOS_SEEK,	F1,	&gui_setconvpos },
	{ SETCONVPOS_UNTIL,	F1,	&gui_setconvpos },

	{ OPEN,	F1 | CMD_FCORE,	&gui_media_open },
	{ ADD,	F1 | CMD_FCORE,	&gui_media_open },
	{ ADDURL,	F1,	&gui_media_addurl },
	{ QUE_NEW,	F0 | CMD_FCORE,	&gui_que_new },
	{ QUE_DEL,	F0 | CMD_FCORE,	&gui_que_del },
	{ QUE_SEL,	F0 | CMD_FCORE,	&gui_que_sel },
	{ SAVELIST,	F0,	&gui_media_savelist },
	{ REMOVE,	F0 | CMD_FCORE,	&gui_media_remove },
	{ CLEAR,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op },
	{ SHOWDIR,	F0,	&gui_media_showdir },
	{ COPYFN,	F0,	&gui_media_copyfn },
	{ COPYFILE,	F1,	&gui_media_fileop },
	{ DELFILE,	F1 | CMD_FCORE,	&gui_media_fileop },
	{ SHOWINFO,	F0 | CMD_FCORE,	&gui_media_showinfo },

	{ CONF_EDIT,	F1,	&gui_showtextfile },
	{ USRCONF_EDIT,	F1,	&gui_showtextfile },
	{ FMEDGUI_EDIT,	F1,	&gui_showtextfile },
	{ README_SHOW,	F1,	&gui_showtextfile },
	{ CHANGES_SHOW,	F1,	&gui_showtextfile },
};

const struct cmd cmd_play = { PLAY,	F1 | CMD_FCORE | CMD_FUDATA,	&gui_corecmd_op };
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

	case PLAY: {
		void *play_id;
		if (NULL == (play_id = gui_list_getent()))
			break;
		gui_corecmd_add(&cmd_play, play_id);
		break;
	}


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
		ffui_tray_show(&gg->wmain.tray_icon, 1);
		ffui_show(&gg->wmain.wmain, 0);
		ffui_show(&gg->winfo.winfo, 0);
		ffui_show(&gg->wgoto.wgoto, 0);
		ffui_show(&gg->wconvert.wconvert, 0);
		ffui_show(&gg->wrec.wrec, 0);
		break;

	case SHOW:
		ffui_show(&gg->wmain.wmain, 1);
		ffui_wnd_setfront(&gg->wmain.wmain);
		ffui_tray_show(&gg->wmain.tray_icon, 0);
		break;

	case ABOUT:
		ffui_show(&gg->wabout.wabout, 1);
		break;

	case QUIT:
	case ONCLOSE:
		gui_corecmd_add(&cmd_quit, NULL);
		gui_onclose();
		if (id == QUIT)
			ffui_wnd_close(&gg->wmain.wmain);
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
};

static void gui_onclose(void)
{
	char *fn;
	ffui_loaderw ldr = {0};

	if (NULL == (fn = ffenv_expand(NULL, 0, GUI_USRCONF)))
		return;

	ldr.getctl = &gui_getctl;
	ldr.udata = gg;
	ffui_ldr_setv(&ldr, setts, FFCNT(setts), 0);

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
	ffui_show(&gg->wgoto.wgoto, 1);
}

/*
Note: if Left/Right key is pressed while trackbar is focused, SEEK command will be received after RWND/FFWD. */
void gui_seek(uint cmd)
{
	if (gg->curtrk == NULL || gg->curtrk->conversion)
		return;

	switch (cmd) {
	case GOTO:
		break;

	case FFWD:
		ffui_trk_move(&gg->wmain.tpos, FFUI_TRK_PGUP);
		break;

	case RWND:
		ffui_trk_move(&gg->wmain.tpos, FFUI_TRK_PGDN);
		break;

	case GOPOS:
		if (gg->go_pos == (uint)-1)
			return;
		ffui_trk_set(&gg->wmain.tpos, gg->go_pos);
		break;
	}

	uint pos = ffui_trk_val(&gg->wmain.tpos);
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

	if (NULL == (ent = gui_list_getent()))
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
	if (NULL == (exp = ffenv_expand(NULL, 0, p)))
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
	ffui_viewitem it;
	ffarr buf = {0};

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
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

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
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
			ffui_redraw(&gg->wmain.vlist, 0);
			FFARR_WALK(&ents, pent) {
				gg->qu->cmd(FMED_QUE_RM, *pent);
			}
			ffui_redraw(&gg->wmain.vlist, 1);
			n = ffs_fmt(st, st + sizeof(st), "Deleted %L files", buf.len);
			gui_status(st, n);
		}
		break;
	}

done:
	ffarr_free(&buf);
}

static void gui_showtextfile(uint cmd)
{
	char *notepad, *fn;

	if (NULL == (notepad = ffenv_expand(NULL, 0, "%SYSTEMROOT%\\system32\\notepad.exe")))
		return;

	switch (cmd) {
	case CONF_EDIT:
		fn = core->getpath(FFSTR("fmedia.conf"));
		break;
	case USRCONF_EDIT:
		fn = ffenv_expand(NULL, 0, "%APPDATA%/fmedia/fmedia.conf");
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
		ffui_view_settext_q(&it, s + FFSLEN("> "));
		ffui_view_set(&gg->wmain.vlist, H_IDX, &it);
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
		ffpath_split2(ent->url.ptr, ent->url.len, NULL, &title);
		ffpath_splitname(title.ptr, title.len, &title, NULL);
	} else
		title = *val;


	ffui_view_settextstr(&it, &artist);
	ffui_view_set(&gg->wmain.vlist, H_ART, &it);

	ffui_view_settextstr(&it, &title);
	ffui_view_set(&gg->wmain.vlist, H_TIT, &it);

	if (flags & GUI_TRKINFO_DUR) {
		n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u", sec / 60, sec % 60);
		ffui_view_settext(&it, buf, n);
		ffui_view_set(&gg->wmain.vlist, H_DUR, &it);
	}

	if (flags & GUI_TRKINFO_WNDCAPTION) {
		n = ffs_fmt(buf, buf + sizeof(buf), "%S - %S - fmedia", &artist, &title);
		ffui_settext(&gg->wmain.wmain, buf, n);
	}
}

void gui_media_added(fmed_que_entry *ent, uint flags)
{
	char buf[255];
	ffui_viewitem it;
	ffmem_tzero(&it);

	int idx = -1;
	if (ent->prev != NULL)
		idx = ffui_view_search(&gg->wmain.vlist, (size_t)ent->prev);

	if (idx == -1) {
		gui_list_add(&it, (size_t)ent);
		idx = it.item.iItem;
	} else {
		size_t n = ffs_fromint(idx + 2, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		ffui_view_setparam(&it, (size_t)ent);
		ffui_view_ins(&gg->wmain.vlist, ++idx, &it);
	}

	if (ent->dur != 0)
		flags |= GUI_TRKINFO_DUR;
	gui_trk_setinfo(idx, ent, ent->dur / 1000, flags);
}

static void gui_media_open(uint id)
{
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_INPUT);
	if (NULL == (fn = ffui_dlg_open(&gg->dlg, &gg->wmain.wmain)))
		return;

	if (id == OPEN)
		gg->qu->cmd(FMED_QUE_CLEAR, NULL);

	ffui_redraw(&gg->wmain.vlist, 0);

	do {
		gui_media_add1(fn);

	} while (NULL != (fn = ffui_dlg_nextname(&gg->dlg)));

	ffui_redraw(&gg->wmain.vlist, 1);

	if (id == OPEN)
		gui_corecmd_op(NEXT, NULL);
}

static void gui_media_addurl(uint id)
{
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

static void gui_showque(uint i)
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
	ffui_viewitem it;

	ffui_redraw(&gg->wmain.vlist, 0);

	first = ffui_view_selnext(&gg->wmain.vlist, -1);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, -1))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
		id = (void*)ffui_view_param(&it);
		gg->qu->cmd2(FMED_QUE_RM | FMED_QUE_NO_ONCHANGE, id, 0);
		ffui_view_rm(&gg->wmain.vlist, i);
	}

	if (first != -1)
		gui_plist_recount(first);
	ffui_redraw(&gg->wmain.vlist, 1);
}

static fmed_que_entry* gui_list_getent(void)
{
	int focused;
	ffui_viewitem it = {0};
	size_t entid;
	if (-1 == (focused = ffui_view_focused(&gg->wmain.vlist)))
		return NULL;
	ffui_view_setindex(&it, focused);
	ffui_view_setparam(&it, 0);
	ffui_view_get(&gg->wmain.vlist, 0, &it);
	if (0 == (entid = ffui_view_param(&it)))
		return NULL;
	return (void*)entid;
}

static void gui_list_add(ffui_viewitem *it, size_t par)
{
	char buf[FFINT_MAXCHARS];
	size_t n = ffs_fromint(ffui_view_nitems(&gg->wmain.vlist) + 1, buf, sizeof(buf), 0);
	ffui_view_settext(it, buf, n);
	ffui_view_setparam(it, par);
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
	const char *fn;

	ffui_redraw(&gg->wmain.vlist, 0);

	while (NULL != (fn = ffui_fdrop_next(df))) {
		gui_media_add1(fn);
	}

	ffui_redraw(&gg->wmain.vlist, 1);
}


/** Update window caption and playlist item with a new meta. */
int gui_setmeta(gui_trk *g, fmed_que_entry *plid)
{
	ssize_t idx;
	if (-1 == (idx = ffui_view_search(&gg->wmain.vlist, (size_t)plid)))
		return -1;

	if (g == NULL) {
		gui_trk_setinfo(idx, plid, 0, GUI_TRKINFO_STOPPED);
		return idx;
	}

	gui_trk_setinfo(idx, plid, g->total_time_sec, GUI_TRKINFO_WNDCAPTION | GUI_TRKINFO_PLAYING | GUI_TRKINFO_DUR);
	return idx;
}

/** Update all properties of the playlist item with a new info. */
void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid)
{
	ffui_viewitem it = {0};
	const char *sval;
	char buf[1024];
	size_t n;
	ssize_t idx;

	if (-1 == (idx = gui_setmeta(g, plid)))
		return;
	ffui_view_setindex(&it, idx);
	ffui_view_focus(&it, 1);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %s, %s"
		, (d->audio.bitrate + 500) / 1000
		, (FMED_PNULL != (sval = d->track->getvalstr(d->trk, "pcm_decoder"))) ? sval : ""
		, g->sample_rate
		, ffpcm_fmtstr(d->audio.fmt.format)
		, ffpcm_channelstr(d->audio.fmt.channels));
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&gg->wmain.vlist, H_INF, &it);

	ffui_trk_setrange(&gg->wmain.tpos, g->total_time_sec);
}
