/** Main window.
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/path.h>


struct gui_wmain {
	ffui_wnd wnd;
	ffui_menu mm;
	ffui_btn bpause
		, bstop
		, bprev
		, bnext;
	ffui_label lpos;
	ffui_trkbar tvol;
	ffui_trkbar tpos;
	ffui_tab tabs;
	ffui_view vlist;
	ffui_ctl stbar;
	ffui_trayicon tray_icon;

	fmed_que_entry *active_qent;

	ffstr exp_path; // path with trailing '/'
	ffvec exp_files; // struct exp_file[]
	ffbyte exp_conf_flags[2];
	ffbyte exp_disable;
	int exp_tab;
};

const ffui_ldr_ctl wmain_ctls[] = {
	FFUI_LDR_CTL(struct gui_wmain, wnd),
	FFUI_LDR_CTL(struct gui_wmain, mm),
	FFUI_LDR_CTL(struct gui_wmain, bpause),
	FFUI_LDR_CTL(struct gui_wmain, bstop),
	FFUI_LDR_CTL(struct gui_wmain, bprev),
	FFUI_LDR_CTL(struct gui_wmain, bnext),
	FFUI_LDR_CTL(struct gui_wmain, lpos),
	FFUI_LDR_CTL(struct gui_wmain, tvol),
	FFUI_LDR_CTL(struct gui_wmain, tpos),
	FFUI_LDR_CTL(struct gui_wmain, tabs),
	FFUI_LDR_CTL(struct gui_wmain, vlist),
	FFUI_LDR_CTL(struct gui_wmain, stbar),
	FFUI_LDR_CTL(struct gui_wmain, tray_icon),
	FFUI_LDR_CTL_END
};

static void wmain_action(ffui_wnd *wnd, int id);
static uint tab_new(uint flags);
static void list_new();
static void list_chooseaddfiles();
static void list_del();
static void list_save();
static void hidetotray();
static void list_dispinfo(struct ffui_view_disp *disp);
static void list_cols_width_load();

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

#include <gui-gtk/file-explorer.h>

void wmain_init()
{
	struct gui_wmain *w = ffmem_new(struct gui_wmain);
	gg->wmain = w;
	w->vlist.dispinfo_id = LIST_DISPINFO;
	w->wnd.on_action = &wmain_action;
	w->wnd.onclose_id = A_ONCLOSE;
}

void wmain_destroy()
{
	exp_free();
	ffmem_free(gg->wmain);
}

void wmain_show()
{
	struct gui_wmain *w = gg->wmain;
	gg->vol = ffui_trk_val(&w->tvol);
	list_cols_width_load();

	ffui_dlg_multisel(&gg->dlg, 1);
	ffui_view_dragdrop(&w->vlist, A_ONDROPFILE);
	ffui_view_popupmenu(&w->vlist, &gg->mpopup);
	exp_tab_new();
	tab_new(0);
	ffui_tab_setactive(&w->tabs, (w->exp_tab+1));
	ffui_show(&w->wnd, 1);
}

/** Called before leaving the current playlist. */
static void list_onleave()
{
	struct gui_wmain *w = gg->wmain;
	if ((w->exp_tab+1) != ffui_tab_active(&w->tabs))
		return;
	gg->conf.list_scroll_pos = ffui_view_scroll_vert(&w->vlist);
}

static void wmain_action(ffui_wnd *wnd, int id)
{
	struct gui_wmain *w = gg->wmain;
	dbglog("%s cmd:%u", __func__, id);

	switch (id) {
	case A_LIST_NEW:
		list_onleave();
		list_new();
		return;
	case A_LIST_DEL:
		list_del();
		return;
	case A_LIST_SEL:
		list_onleave();
		ffui_view_clear(&w->vlist);
		if ((int)w->tabs.changed_index == w->exp_tab) {
			exp_list_show();
		} else {
			ffui_view_popupmenu(&w->vlist, &gg->mpopup);
			corecmd_add(A_LIST_SEL, (void*)(size_t)(w->tabs.changed_index - (w->exp_tab+1)));
		}
		return;
	case A_LIST_ADDFILE:
		list_chooseaddfiles();
		return;
	case A_LIST_ADDURL:
		wuri_show(1);
		return;

	case A_FILE_SHOWINFO: {
		ffui_sel *sel = ffui_view_getsel(&w->vlist);
		int i = ffui_view_selnext(&w->vlist, sel);
		if (i != -1)
			winfo_show(1, i);
		ffui_view_sel_free(sel);
		return;
	}

	case A_FILE_SHOWPCM:
	case A_FILE_SHOWDIR:
	case A_FILE_DELFILE:
		break;

	case A_SHOW:
		ffui_show(&w->wnd, 1);
		ffui_tray_show(&w->tray_icon, 0);
		return;
	case A_HIDE:
		hidetotray();
		return;

	case A_QUIT:
		ffui_wnd_close(&w->wnd);
		return;

	case A_PLAY:
		gg->focused = ffui_view_focused(&w->vlist);
		if (gg->focused == -1)
			return;
		if (ffui_tab_active(&w->tabs) == w->exp_tab) {
			exp_list_action(gg->focused);
			return;
		}
		break;

	case A_PLAYPAUSE:
		corecmd_add(A_PLAYPAUSE, gg->curtrk);
		return;

	case A_STOP:
	case A_STOP_AFTER:
	case A_NEXT:
	case A_PREV:
	case A_PLAY_REPEAT:
		break;

	case A_SEEK: {
		int val = ffui_trk_val(&w->tpos);
		corecmd_add(id, (void*)(size_t)val);
		return;
	}
	case A_FFWD:
	case A_RWND:
	case A_LEAP_FWD:
	case A_LEAP_BACK:
	case A_SETGOPOS:
	case A_GOPOS:
		break;
	case A_VOLUP:
		ffui_trk_set(&w->tvol, gg->vol + 5);
		break;
	case A_VOLDOWN:
		ffui_trk_set(&w->tvol, gg->vol - 5);
		break;
	case A_VOLRESET:
		ffui_trk_set(&w->tvol, 100);
		break;
	case A_VOL: {
		int val = ffui_trk_val(&w->tvol);
		corecmd_add(id, (void*)(size_t)val);
		return;
	}

	case A_EXPL_ADD:
	case A_EXPL_ADDPLAY:
		exp_action(id);
		return;

	case A_SHOW_PROPS:
		wplayprops_show(1);
		return;

	case A_DLOAD_SHOW:
		wdload_show(1);
		return;

	case A_SHOW_RENAME:
		wrename_show(1);
		return;

	case A_LIST_SAVE:
		list_save();
		return;
	case A_LIST_SELECTALL:
		ffui_view_selall(&w->vlist);
		return;
	case A_LIST_TO_NEXT:
		if (w->exp_tab == ffui_tab_active(&w->tabs))
			return;
		break;
	case A_LIST_READMETA:
	case A_LIST_REMOVE:
	case A_LIST_RMDEAD:
	case A_LIST_CLEAR:
	case A_LIST_RANDOM:
	case A_LIST_SORTRANDOM:
		break;

	case A_ONCLOSE:
		ctlconf_write();
		usrconf_write();
		ffui_post_quitloop();
		return;

	case A_ONDROPFILE: {
		ffstr *d = ffmem_new(ffstr);
		ffstr_alcopystr(d, &w->vlist.drop_data);
		corecmd_add(id, d);
		return;
	}

	case A_ABOUT:
		wabout_show(1);
		return;

	case A_SHOWCONVERT:
		wconv_show(1);
		return;

	case A_CONV_SET_SEEK:
	case A_CONV_SET_UNTIL: {
		int pos = ffui_trk_val(&w->tpos);
		wconv_setdata(id, pos);
		return;
	}

	case A_CMD_SHOW:
		wcmd_show(1);
		return;

	case A_CONF_EDIT:
	case A_USRCONF_EDIT:
	case A_FMEDGUI_EDIT:
	case A_README_SHOW:
	case A_CHANGES_SHOW:
		gui_showtextfile(id);
		return;

	case LIST_DISPINFO:
		if (ffui_tab_active(&w->tabs) == w->exp_tab)
			exp_list_dispinfo(&w->vlist.disp);
		else
			list_dispinfo(&w->vlist.disp);
		return;

	default:
		FF_ASSERT(0);
		return;
	}
	corecmd_add(id, NULL);
}

void wmain_cmd(int id)
{
	struct gui_wmain *w = gg->wmain;
	wmain_action(&w->wnd, id);
}

static const char* const list_colname[] = {
	NULL, //H_IDX
	"artist", //H_ART
	"title", //H_TIT
	"__dur", //H_DUR
	"__info", //H_INF
	"date", //H_DATE
	"album", //H_ALBUM
	"", //H_FN
};

static void list_dispinfo(struct ffui_view_disp *disp)
{
	struct gui_wmain *w = gg->wmain;
	fmed_que_entry *ent;
	char buf[256];
	ffstr *val = NULL, s;
	uint sub = disp->sub;

	ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, disp->idx);
	if (ent == NULL)
		return;

	switch (sub) {
	case H_IDX:
		ffs_fmt(buf, buf + sizeof(buf), "%s%u%Z"
			, (ent == w->active_qent) ? "> " : ""
			, disp->idx + 1);
		ffstr_setz(&s, buf);
		val = &s;
		break;

	case H_FN:
		val = &ent->url;
		break;

	default:
		val = gg->qu->meta_find(ent, list_colname[sub], ffsz_len(list_colname[sub]));
	}

	switch (sub) {
	case H_TIT:
		if (val == NULL || val->len == 0) {
			//use filename as a title
			ffpath_split3(ent->url.ptr, ent->url.len, NULL, &s, NULL);
			val = &s;
		}
		break;

	case H_DUR:
		if (val == NULL && ent->dur != 0) {
			uint sec = ent->dur / 1000;
			ffs_fmt(buf, buf + sizeof(buf), "%u:%02u%Z", sec / 60, sec % 60);
			val = &s;
			ffstr_setz(val, buf);
		}
		break;
	}

	if (val != NULL)
		disp->text.len = ffs_append(disp->text.ptr, 0, disp->text.len, val->ptr, val->len);

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);
}

void wmain_list_update(uint idx, int delta)
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return;
	// dbglog("update: ffui_view_setdata %d", delta);
	ffui_send_view_setdata(&w->vlist, idx, delta);
}

static void list_setdata_scroll(void *param)
{
	struct gui_wmain *w = gg->wmain;
	ffui_view_clear(&w->vlist);
	// dbglog("ffui_view_setdata =%L", (ffsize)param);
	ffui_view_setdata(&w->vlist, 0, (size_t)param);
}

void wmain_list_set(uint idx, int delta)
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return;
	ffui_thd_post(&list_setdata_scroll, (void*)(size_t)delta, FFUI_POST_WAIT);
	if (ffui_send_tab_active(&w->tabs) == (w->exp_tab+1) && gg->conf.list_scroll_pos != 0) {
		// ffui_view_scroll_setvert() doesn't work from list_setdata_scroll()
		ffui_post_view_scroll_set(&w->vlist, gg->conf.list_scroll_pos);
	}
}

void wmain_ent_added(uint idx)
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return;
	// dbglog("ffui_view_setdata +1");
	ffui_send_view_setdata(&w->vlist, idx, 1);
}

void wmain_ent_removed(uint idx)
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return;
	// dbglog("ffui_view_setdata -1");
	ffui_send_view_setdata(&w->vlist, idx, -1);
}

void wmain_newtrack(fmed_que_entry *ent, uint time_total, fmed_filt *d)
{
	struct gui_wmain *w = gg->wmain;
	int conversion = (FMED_PNULL != d->track->getvalstr(d->trk, "output"));

	char buf[1024];
	size_t n;
	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %s, %s"
		, (d->audio.bitrate + 500) / 1000
		, d->audio.decoder
		, d->audio.fmt.sample_rate
		, ffpcm_fmtstr(d->audio.fmt.format)
		, ffpcm_channelstr(d->audio.fmt.channels));
	gg->qu->meta_set(ent, FFSTR("__info"), buf, n, FMED_QUE_PRIV | FMED_QUE_OVWRITE);

	if (conversion) {
		int idx = gg->qu->cmdv(FMED_QUE_ID, ent);
		if (idx != -1)
			ffui_send_view_setdata(&w->vlist, idx, 0);
		return;
	}

	ffui_post_trk_setrange(&w->tpos, time_total);
	ent->dur = time_total * 1000;

	void *active_qent = w->active_qent;
	w->active_qent = ent;
	int idx = gg->qu->cmdv(FMED_QUE_ID, active_qent);
	if (idx != -1)
		ffui_send_view_setdata(&w->vlist, idx, 0);

	idx = gg->qu->cmdv(FMED_QUE_ID, ent);
	if (idx != -1)
		ffui_send_view_setdata(&w->vlist, idx, 0);

	ffstr artist = {}, title = {}, *val;
	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("artist"))))
		artist = *val;

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("title")))
		&& val->len != 0) {
		title = *val;
	} else {
		//use filename as a title
		ffpath_split3(ent->url.ptr, ent->url.len, NULL, &title, NULL);
	}

	char *sz = ffsz_alfmt("%S - %S - fmedia", &artist, &title);
	ffui_send_wnd_settext(&w->wnd, sz);
	ffmem_free(sz);
}

void wmain_fintrack()
{
	struct gui_wmain *w = gg->wmain;
	if (w->active_qent != NULL) {
		int idx = gg->qu->cmdv(FMED_QUE_ID, w->active_qent);
		w->active_qent = NULL;
		if (idx >= 0)
			ffui_send_view_setdata(&w->vlist, idx, 0);
	}
	ffui_send_wnd_settext(&w->wnd, "fmedia");
	ffui_send_lbl_settext(&w->lpos, "");
	ffui_post_trk_setrange(&w->tpos, 0);
}

void wmain_update(uint playtime, uint time_total)
{
	struct gui_wmain *w = gg->wmain;
	ffui_post_trk_set(&w->tpos, playtime);

	char buf[256];
	ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u%Z"
		, playtime / 60, playtime % 60
		, time_total / 60, time_total % 60);
	ffui_send_lbl_settext(&w->lpos, buf);
}

void wmain_update_convert(fmed_que_entry *plid, uint playtime, uint time_total)
{
	struct gui_wmain *w = gg->wmain;
	ffssize idx;
	if (-1 == (idx = gg->qu->cmdv(FMED_QUE_ID, plid))) {
		FF_ASSERT(0);
		return;
	}

	char buf[255];
	ffstr val;
	if (playtime == (uint)-1) {
		ffstr_setz(&val, "Done");
	} else {
		ffsize n = ffs_format_r0(buf, sizeof(buf), "%u:%02u / %u:%02u"
			, playtime / 60, playtime % 60
			, time_total / 60, time_total % 60);
		ffstr_set(&val, buf, n);
	}
	gg->qu->meta_set(plid, FFSTR("__dur"), val.ptr, val.len, FMED_QUE_PRIV | FMED_QUE_OVWRITE);
	ffui_post_view_setdata(&w->vlist, idx, 0);
}

/** Set status bar text. */
void wmain_status(const char *fmt, ...)
{
	struct gui_wmain *w = gg->wmain;
	va_list va;
	va_start(va, fmt);
	char *s = ffsz_alfmtv(fmt, va);
	va_end(va);
	ffui_send_stbar_settextz(&w->stbar, s);
	ffmem_free(s);
}

void wmain_list_clear()
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return;
	ffui_post_view_clear(&w->vlist);
}

/** Create a new tab. */
uint tab_new(uint flags)
{
	struct gui_wmain *w = gg->wmain;
	char buf[32];
	ffs_fmt(buf, buf + sizeof(buf), "Playlist %u%Z", ++gg->tabs_counter);
	ffui_tab_append(&w->tabs, buf);
	return ffui_tab_count(&w->tabs);
}

/** Create a new tab. */
ffuint wmain_tab_new(ffuint flags)
{
	struct gui_wmain *w = gg->wmain;
	char buf[32];
	if (flags & TAB_CONVERT)
		ffsz_copyz(buf, sizeof(buf), "Converting...");
	else
		ffs_fmt(buf, buf + sizeof(buf), "Playlist %u%Z", ++gg->tabs_counter);

	ffui_send_tab_ins(&w->tabs, buf);
	return ffui_send_tab_count(&w->tabs) - 1 - (w->exp_tab+1);
}

int wmain_tab_active()
{
	struct gui_wmain *w = gg->wmain;
	return ffui_send_tab_active(&w->tabs) - (w->exp_tab+1);
}

/** Create a new tab and playlist. */
void list_new()
{
	struct gui_wmain *w = gg->wmain;
	uint n = tab_new(0);
	n--;
	ffui_tab_setactive(&w->tabs, n);
	ffui_view_clear(&w->vlist);
	corecmd_add(A_LIST_NEW, (void*)(size_t)(n - (w->exp_tab+1)));
}

void wmain_list_select(ffuint idx)
{
	struct gui_wmain *w = gg->wmain;
	ffui_send_tab_setactive(&w->tabs, idx + (w->exp_tab+1));
}

/** Remove the current tab and activate its neighbour or create a new tab. */
void list_del()
{
	struct gui_wmain *w = gg->wmain;
	int i = ffui_tab_active(&w->tabs);
	if (i == w->exp_tab)
		return;
	ffbool last = ((w->exp_tab+1) + 1 == ffui_tab_count(&w->tabs));
	if (last) {
		wmain_action(&w->wnd, A_LIST_NEW);
	} else {
		int first = (i == (w->exp_tab+1));
		uint newsel = (first) ? i + 1 : i - 1;
		ffui_tab_setactive(&w->tabs, newsel);
		ffui_view_clear(&w->vlist);
		corecmd_add(A_LIST_SEL, (void*)(size_t)(newsel - (w->exp_tab+1)));
	}
	ffui_tab_del(&w->tabs, i);
	corecmd_add(A_LIST_DEL, (void*)(size_t)(i - (w->exp_tab+1)));
}

/** Add the files chosen by user */
void list_chooseaddfiles()
{
	struct gui_wmain *w = gg->wmain;
	char *fn;
	if (NULL == (fn = ffui_dlg_open(&gg->dlg, &w->wnd)))
		return;

	ffstr *s;
	for (;;) {
		s = ffmem_new(ffstr);
		ffstr_alcopyz(s, fn);
		corecmd_add(A_URL_ADD, s);
		if (NULL == (fn = ffui_dlg_nextname(&gg->dlg)))
			break;
	}
}

/** User chooses file to save the current playlist to. */
void list_save()
{
	struct gui_wmain *w = gg->wmain;
	char *fn;
	ffstr name;
	ffstr_setz(&name, "Playlist.m3u8");
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &w->wnd, name.ptr, name.len)))
		return;

	char *list_fn;
	if (NULL == (list_fn = ffsz_alcopyz(fn)))
		return;
	corecmd_add(A_LIST_SAVE, list_fn);
}

void hidetotray()
{
	struct gui_wmain *w = gg->wmain;
	ffui_show(&w->wnd, 0);
	wabout_show(0);
	wcmd_show(0);
	wconv_show(0);
	wdload_show(0);
	winfo_show(0, 0);
	wlog_show(0);
	wplayprops_show(0);
	wrename_show(0);
	wuri_show(0);
	if (!ffui_tray_hasicon(&w->tray_icon)) {
		ffui_icon ico;
		char *fn = core->getpath(FFSTR("fmedia.ico"));
		ffui_icon_load(&ico, fn);
		ffmem_free(fn);
		ffui_tray_seticon(&w->tray_icon, &ico);
	}
	ffui_tray_show(&w->tray_icon, 1);
}

/** Load widths of list's columns. */
static void list_cols_width_load()
{
	struct gui_wmain *w = gg->wmain;
	ffui_viewcol vc = {};
	ffui_viewcol_reset(&vc);
	for (uint i = 0;  i != _H_LAST;  i++) {
		FF_ASSERT(i != FFCNT(gg->conf.list_col_width));
		if (gg->conf.list_col_width[i] == 0)
			continue;

		ffui_viewcol_setwidth(&vc, gg->conf.list_col_width[i]);
		ffui_view_setcol(&w->vlist, i, &vc);
	}
}

/** Write widths of list's columns to config. */
void wmain_list_cols_width_write(ffconfw *conf)
{
	struct gui_wmain *w = gg->wmain;
	ffui_viewcol vc = {};
	for (uint i = 0;  i != _H_LAST;  i++) {
		ffui_viewcol_reset(&vc);
		ffui_viewcol_setwidth(&vc, 0);
		ffui_view_col(&w->vlist, i, &vc);
		ffconfw_addint(conf, ffui_viewcol_width(&vc));
	}
}

ffui_sel* wmain_list_getsel()
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return ffmem_new(ffui_sel);
	return (void*)ffui_view_getsel(&w->vlist);
}

ffui_sel* wmain_list_getsel_send()
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) == w->exp_tab)
		return ffmem_new(ffui_sel);
	return (void*)ffui_send_view_getsel(&w->vlist);
}

int wmain_list_scroll_vert()
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_send_tab_active(&w->tabs) != (w->exp_tab+1))
		return gg->conf.list_scroll_pos;
	return ffui_view_scroll_vert(&w->vlist);
}
