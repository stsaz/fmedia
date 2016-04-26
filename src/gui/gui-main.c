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
static void gui_media_remove(void);
static fmed_que_entry* gui_list_getent(void);
static void gui_go_set(void);
static void gui_seek(uint cmd);
static void gui_media_seek(gui_trk *g, uint cmd);
static void gui_vol(uint id);
static void gui_media_vol(gui_trk *g, uint id);
static void gui_media_showdir(void);
static void gui_media_copyfn(void);
static void gui_media_fileop(uint cmd);
static void gui_showtextfile(uint cmd);
static void gui_on_dropfiles(ffui_wnd *wnd, ffui_fdrop *df);
static void gui_onclose(void);
static int gui_newtab(void);


void wmain_init(void)
{
	ffui_show(&gg->wmain.wmain, 1);
	gg->wmain.wmain.top = 1;
	gg->wmain.wmain.on_action = &gui_action;
	gg->wmain.wmain.onclose_id = ONCLOSE;
	gui_newtab();
	ffui_tray_settooltipz(&gg->wmain.tray_icon, "fmedia");
	gg->wmain.vlist.colclick_id = SORT;
	gg->wmain.wmain.on_dropfiles = &gui_on_dropfiles;
	ffui_fdrop_accept(&gg->wmain.wmain, 1);
}


static const struct cmd cmds[] = {
	{ STOP,	F1,	&gui_task_add },
	{ STOP_AFTER,	F1,	&gui_task_add },
	{ NEXT,	F1,	&gui_task_add },
	{ PREV,	F1,	&gui_task_add },

	{ SEEK,	F1,	&gui_seek },
	{ FFWD,	F1,	&gui_seek },
	{ RWND,	F1,	&gui_seek },
	{ GOPOS,	F1,	&gui_seek },
	{ SETGOPOS,	F0,	&gui_go_set },

	{ VOL,	F1,	&gui_vol },
	{ VOLUP,	F1,	&gui_vol },
	{ VOLDOWN,	F1,	&gui_vol },

	{ REC,	F1,	&gui_task_add },
	{ PLAYREC,	F1,	&gui_task_add },
	{ MIXREC,	F1,	&gui_task_add },

	{ SHOWCONVERT,	F0,	&gui_showconvert },

	{ OPEN,	F1,	&gui_media_open },
	{ ADD,	F1,	&gui_media_open },
	{ QUE_NEW,	F1,	&gui_task_add },
	{ QUE_DEL,	F1,	&gui_task_add },
	{ QUE_SEL,	F1,	&gui_task_add },
	{ SAVELIST,	F0,	&gui_media_savelist },
	{ REMOVE,	F0,	&gui_media_remove },
	{ SHOWDIR,	F0,	&gui_media_showdir },
	{ COPYFN,	F0,	&gui_media_copyfn },
	{ COPYFILE,	F1,	&gui_media_fileop },
	{ DELFILE,	F1,	&gui_media_fileop },
	{ SHOWINFO,	F0,	&gui_media_showinfo },

	{ CONF_EDIT,	F1,	&gui_showtextfile },
	{ USRCONF_EDIT,	F1,	&gui_showtextfile },
	{ FMEDGUI_EDIT,	F1,	&gui_showtextfile },
	{ README_SHOW,	F1,	&gui_showtextfile },
	{ CHANGES_SHOW,	F1,	&gui_showtextfile },
};

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
	gui_trk *g = gg->curtrk;

	const struct cmd *cmd = getcmd(id, cmds, FFCNT(cmds));
	if (cmd != NULL) {
		cmdfunc_u u;
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
		uint pos = ffui_trk_val(&gg->wmain.tpos);
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
		ffui_view_sel(&gg->wmain.vlist, -1);
		break;

	case SELINVERT:
		ffui_view_sel_invert(&gg->wmain.vlist);
		break;

	case SORT:
		if (gg->wmain.vlist.col == H_TIT || gg->wmain.vlist.col == H_ART || gg->wmain.vlist.col == H_FN)
			ffui_view_sort(&gg->wmain.vlist, &gui_list_sortfunc, gg->wmain.vlist.col);
		break;

	case CLEAR:
		gg->qu->cmd(FMED_QUE_CLEAR, NULL);
		ffui_view_clear(&gg->wmain.vlist);
		break;

	case HIDE:
		ffui_tray_show(&gg->wmain.tray_icon, 1);
		ffui_show(&gg->wmain.wmain, 0);
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
		gui_task_add(QUIT);
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

/*
Note: if Left/Right key is pressed while trackbar is focused, SEEK command will be received after RWND/FFWD. */
static void gui_seek(uint cmd)
{
	switch (cmd) {
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

	fflk_lock(&gg->lktrk);
	if (gg->curtrk != NULL && pos != gg->curtrk->seekpos && !gg->curtrk->conversion) {
		gg->curtrk->seekpos = pos;
		gui_addcmd(&gui_media_seek, cmd);
	}
	fflk_unlock(&gg->lktrk);
}

static void gui_media_seek(gui_trk *g, uint cmd)
{
	gg->track->setval(g->trk, "seek_time", g->seekpos * 1000);
	g->seekpos = (uint)-1;
	gg->track->setval(g->trk, "snd_output_clear", 1);
	g->goback = 1;
}

static void gui_vol(uint id)
{
	char buf[64];
	uint pos;
	double db;
	size_t n;

	switch (id) {
	case VOLUP:
		ffui_trk_move(&gg->wmain.tvol, FFUI_TRK_PGUP);
		break;

	case VOLDOWN:
		ffui_trk_move(&gg->wmain.tvol, FFUI_TRK_PGDN);
		break;
	}

	pos = ffui_trk_val(&gg->wmain.tvol);
	if (pos <= 100)
		db = ffpcm_vol2db(pos, 48);
	else
		db = ffpcm_vol2db_inc(pos - 100, 25, 6);
	n = ffs_fmt(buf, buf + sizeof(buf), "Volume: %.02FdB", db);
	gui_status(buf, n);

	fflk_lock(&gg->lktrk);
	if (gg->curtrk != NULL && !gg->curtrk->conversion) {
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

void gui_media_added(fmed_que_entry *ent)
{
	ffstr name;
	ffui_viewitem it;
	ffmem_tzero(&it);
	gui_list_add(&it, (size_t)ent);
	ffui_view_settextstr(&it, &ent->url);
	ffui_view_set(&gg->wmain.vlist, H_FN, &it);

	ffpath_split2(ent->url.ptr, ent->url.len, NULL, &name);
	ffui_view_settextstr(&it, &name);
	ffui_view_set(&gg->wmain.vlist, H_TIT, &it);
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
		gui_task_add(NEXT);
}

void gui_media_removed(uint i)
{
	ffui_viewitem it;
	char buf[FFINT_MAXCHARS];
	size_t n;

	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_rm(&gg->wmain.vlist, i);

	for (;  ;  i++) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		n = ffs_fromint(i + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		if (0 != ffui_view_set(&gg->wmain.vlist, H_IDX, &it))
			break;
	}

	ffui_redraw(&gg->wmain.vlist, 1);
}

static int gui_newtab(void)
{
	char buf[32];
	static uint tabs;
	ffui_tabitem it = {0};

	int n = ffs_fmt(buf, buf + sizeof(buf), "Playlist %u", ++tabs);
	ffui_tab_settext(&it, buf, n);
	int itab = ffui_tab_append(&gg->wmain.tabs, &it);
	ffui_tab_setactive(&gg->wmain.tabs, itab);
	return itab;
}

void gui_que_new(void)
{
	int itab = gui_newtab();
	gg->qu->cmd(FMED_QUE_NEW, NULL);
	ffui_view_clear(&gg->wmain.vlist);
	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)itab);
}

static void gui_showque(uint i)
{
	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)i);

	ffui_redraw(&gg->wmain.vlist, 0);
	ffui_view_clear(&gg->wmain.vlist);
	fmed_que_entry *e = NULL;
	for (;;) {
		if (0 == gg->qu->cmd2(FMED_QUE_LIST, &e, 0))
			break;
		gui_media_added(e);
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

	if (NULL == (gg->list_fn = ffsz_alcopyz(fn)))
		return;
	gui_task_add(SAVELIST);
}

static void gui_media_remove(void)
{
	int i;
	void *id;
	ffui_viewitem it;

	ffui_redraw(&gg->wmain.vlist, 0);

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, -1))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
		id = (void*)ffui_view_param(&it);
		gg->qu->cmd(FMED_QUE_RM, id);
	}

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


void gui_newtrack(gui_trk *g, fmed_filt *d, fmed_que_entry *plid)
{
	ffui_viewitem it = {0};
	const char *sval;
	char buf[1024];
	size_t n;
	ssize_t idx = -1;
	ffstr *tstr, artist = {0}, stitle;

	if (-1 != (idx = ffui_view_search(&gg->wmain.vlist, (size_t)plid)))
		ffui_view_setindex(&it, idx);

	ffui_view_focus(&it, 1);
	ffui_view_set(&gg->wmain.vlist, H_IDX, &it);

	if (NULL != (tstr = gg->qu->meta_find(plid, FFSTR("artist"))))
		artist = *tstr;

	if (NULL == (tstr = gg->qu->meta_find(plid, FFSTR("title")))) {
		//use filename as a title
		ffpath_split2(plid->url.ptr, plid->url.len, NULL, &stitle);
		ffpath_splitname(stitle.ptr, stitle.len, &stitle, NULL);
	} else
		stitle = *tstr;

	n = ffs_fmt(buf, buf + sizeof(buf), "%S - %S - fmedia", &artist, &stitle);
	ffui_settext(&gg->wmain.wmain, buf, n);

	ffui_view_settextstr(&it, &artist);
	ffui_view_set(&gg->wmain.vlist, H_ART, &it);

	ffui_view_settextstr(&it, &stitle);
	ffui_view_set(&gg->wmain.vlist, H_TIT, &it);

	ffui_view_settextstr(&it, &plid->url);
	ffui_view_set(&gg->wmain.vlist, H_FN, &it);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u:%02u"
		, g->total_time_sec / 60, g->total_time_sec % 60);
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&gg->wmain.vlist, H_DUR, &it);

	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %u bit, %s"
		, (int)((d->track->getval(d->trk, "bitrate") + 500) / 1000)
		, (FMED_PNULL != (sval = d->track->getvalstr(d->trk, "pcm_decoder"))) ? sval : ""
		, g->sample_rate
		, ffpcm_bits(d->track->getval(d->trk, "pcm_format"))
		, ffpcm_channelstr((int)d->track->getval(d->trk, "pcm_channels")));
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&gg->wmain.vlist, H_INF, &it);
}
