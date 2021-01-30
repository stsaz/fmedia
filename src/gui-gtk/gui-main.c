/** Main window.
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/path.h>


static void wmain_action(ffui_wnd *wnd, int id);
static uint tab_new(uint flags);
static void list_new();
static void list_chooseaddfiles();
static void list_del();
static void list_save();
static void hidetotray();
static void list_dispinfo(struct ffui_view_disp *disp);
static void list_cols_width_load();


void wmain_init()
{
	gg->vol = ffui_trk_val(&gg->wmain.tvol);
	gg->wmain.vlist.dispinfo_id = LIST_DISPINFO;

	list_cols_width_load();

	ffui_dlg_multisel(&gg->dlg, 1);
	gg->wmain.wmain.on_action = &wmain_action;
	gg->wmain.wmain.onclose_id = A_ONCLOSE;
	ffui_view_dragdrop(&gg->wmain.vlist, A_ONDROPFILE);
	tab_new(0);
	ffui_show(&gg->wmain, 1);
}

/** Called before leaving the current playlist. */
static void list_onleave()
{
	if (0 != ffui_tab_active(&gg->wmain.tabs))
		return;
	gg->conf.list_scroll_pos = ffui_view_scroll_vert(&gg->wmain.vlist);
}

static void wmain_action(ffui_wnd *wnd, int id)
{
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
		ffui_view_clear(&gg->wmain.vlist);
		corecmd_add(A_LIST_SEL, (void*)(size_t)gg->wmain.tabs.changed_index);
		return;
	case A_LIST_ADDFILE:
		list_chooseaddfiles();
		return;
	case A_LIST_ADDURL:
		ffui_show(&gg->wuri.wuri, 1);
		return;

	case A_FILE_SHOWINFO: {
		ffarr4 *sel = ffui_view_getsel(&gg->wmain.vlist);
		int i = ffui_view_selnext(&gg->wmain.vlist, sel);
		if (i != -1)
			winfo_show(i);
		ffui_view_sel_free(sel);
		return;
	}

	case A_FILE_SHOWPCM:
	case A_FILE_SHOWDIR:
	case A_FILE_DELFILE:
		break;

	case A_SHOW:
		ffui_show(&gg->wmain.wmain, 1);
		ffui_tray_show(&gg->wmain.tray_icon, 0);
		return;
	case A_HIDE:
		hidetotray();
		return;

	case A_QUIT:
		ffui_wnd_close(&gg->wmain.wmain);
		return;

	case A_PLAY:
		gg->focused = ffui_view_focused(&gg->wmain.vlist);
		if (gg->focused == -1)
			return;
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
		int val = ffui_trk_val(&gg->wmain.tpos);
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
		ffui_trk_set(&gg->wmain.tvol, gg->vol + 5);
		break;
	case A_VOLDOWN:
		ffui_trk_set(&gg->wmain.tvol, gg->vol - 5);
		break;
	case A_VOLRESET:
		ffui_trk_set(&gg->wmain.tvol, 100);
		break;
	case A_VOL: {
		int val = ffui_trk_val(&gg->wmain.tvol);
		corecmd_add(id, (void*)(size_t)val);
		return;
	}

	case A_SHOW_PROPS:
		wplayprops_show();
		return;

	case A_DLOAD_SHOW:
		ffui_show(&gg->wdload.wdload, 1);
		return;

	case A_LIST_SAVE:
		list_save();
		return;
	case A_LIST_SELECTALL:
		ffui_view_selall(&gg->wmain.vlist);
		return;
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
		ffstr_alcopystr(d, &gg->wmain.vlist.drop_data);
		corecmd_add(id, d);
		return;
	}

	case A_ABOUT:
		ffui_show(&gg->wabout, 1);
		return;

	case A_SHOWCONVERT:
		wconv_show();
		return;

	case A_CONV_SET_SEEK:
	case A_CONV_SET_UNTIL: {
		int pos = ffui_trk_val(&gg->wmain.tpos);
		wconv_setdata(id, pos);
		return;
	}

	case A_CONF_EDIT:
	case A_USRCONF_EDIT:
	case A_FMEDGUI_EDIT:
	case A_README_SHOW:
	case A_CHANGES_SHOW:
		gui_showtextfile(id);
		return;

	case LIST_DISPINFO:
		list_dispinfo(&gg->wmain.vlist.disp);
		return;

	default:
		FF_ASSERT(0);
		return;
	}
	corecmd_add(id, NULL);
}

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
			, (ent == gg->wmain.active_qent) ? "> " : ""
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
		if (val == NULL) {
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
	ffui_send_view_setdata(&gg->wmain.vlist, idx, delta);
}

static void list_setdata_scroll(void *param)
{
	ffui_view_clear(&gg->wmain.vlist);
	ffui_view_setdata(&gg->wmain.vlist, 0, (size_t)param);
	if (ffui_tab_active(&gg->wmain.tabs) == 0 && gg->conf.list_scroll_pos != 0) {
		// ffui_view_scroll_setvert() doesn't work here
		ffui_post_view_scroll_set(&gg->wmain.vlist, gg->conf.list_scroll_pos);
		gg->conf.list_scroll_pos = 0;
	}
}

void wmain_list_set(uint idx, int delta)
{
	ffui_thd_post(&list_setdata_scroll, (void*)(size_t)delta, FFUI_POST_WAIT);
}

void wmain_ent_added(uint idx)
{
	ffui_send_view_setdata(&gg->wmain.vlist, idx, 1);
}

void wmain_ent_removed(uint idx)
{
	ffui_send_view_setdata(&gg->wmain.vlist, idx, -1);
}

void wmain_newtrack(fmed_que_entry *ent, uint time_total, fmed_filt *d)
{
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
			ffui_send_view_setdata(&gg->wmain.vlist, idx, 0);
		return;
	}

	void *active_qent = gg->wmain.active_qent;
	gg->wmain.active_qent = ent;
	int idx = gg->qu->cmdv(FMED_QUE_ID, active_qent);
	if (idx != -1)
		ffui_send_view_setdata(&gg->wmain.vlist, idx, 0);

	idx = gg->qu->cmdv(FMED_QUE_ID, ent);
	if (idx != -1)
		ffui_send_view_setdata(&gg->wmain.vlist, idx, 0);

	ffui_post_trk_setrange(&gg->wmain.tpos, time_total);
	ent->dur = time_total * 1000;

	ffstr artist = {}, title = {}, *val;
	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("artist"))))
		artist = *val;

	if (NULL != (val = gg->qu->meta_find(ent, FFSTR("title"))))
		title = *val;
	else {
		//use filename as a title
		ffpath_split3(ent->url.ptr, ent->url.len, NULL, &title, NULL);
	}

	char *sz = ffsz_alfmt("%S - %S - fmedia", &artist, &title);
	ffui_send_wnd_settext(&gg->wmain.wmain, sz);
	ffmem_free(sz);
}

void wmain_fintrack()
{
	if (gg->wmain.active_qent != NULL) {
		int idx = gg->qu->cmdv(FMED_QUE_ID, gg->wmain.active_qent);
		gg->wmain.active_qent = NULL;
		if (idx >= 0)
			ffui_send_view_setdata(&gg->wmain.vlist, idx, 0);
	}
	ffui_send_wnd_settext(&gg->wmain.wmain, "fmedia");
	ffui_send_lbl_settext(&gg->wmain.lpos, "");
	ffui_post_trk_setrange(&gg->wmain.tpos, 0);
}

void wmain_update(uint playtime, uint time_total)
{
	ffui_post_trk_set(&gg->wmain.tpos, playtime);

	char buf[256];
	ffs_fmt(buf, buf + sizeof(buf), "%u:%02u / %u:%02u%Z"
		, playtime / 60, playtime % 60
		, time_total / 60, time_total % 60);
	ffui_send_lbl_settext(&gg->wmain.lpos, buf);
}

/** Set status bar text. */
void wmain_status(const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	char *s = ffsz_alfmtv(fmt, va);
	va_end(va);
	ffui_send_stbar_settextz(&gg->wmain.stbar, s);
	ffmem_free(s);
}

void wmain_list_clear()
{
	ffui_post_view_clear(&gg->wmain.vlist);
}

/** Create a new tab. */
uint tab_new(uint flags)
{
	char buf[32];
	ffs_fmt(buf, buf + sizeof(buf), "Playlist %u%Z", ++gg->tabs_counter);
	ffui_tab_append(&gg->wmain.tabs, buf);
	return ffui_tab_count(&gg->wmain.tabs);
}

/** Create a new tab. */
ffuint wmain_tab_new(ffuint flags)
{
	char buf[32];
	if (flags & TAB_CONVERT)
		ffsz_copyz(buf, sizeof(buf), "Converting...");
	else
		ffs_fmt(buf, buf + sizeof(buf), "Playlist %u%Z", ++gg->tabs_counter);

	ffui_send_tab_ins(&gg->wmain.tabs, buf);
	return ffui_send_tab_count(&gg->wmain.tabs) - 1;
}

/** Create a new tab and playlist. */
void list_new()
{
	uint n = tab_new(0);
	n--;
	ffui_tab_setactive(&gg->wmain.tabs, n);
	ffui_view_clear(&gg->wmain.vlist);
	corecmd_add(A_LIST_NEW, (void*)(size_t)n);
}

void wmain_list_select(ffuint idx)
{
	ffui_send_tab_setactive(&gg->wmain.tabs, idx);
}

/** Remove the current tab and activate its neighbour or create a new tab. */
void list_del()
{
	int i = ffui_tab_active(&gg->wmain.tabs);
	ffbool last = (1 == ffui_tab_count(&gg->wmain.tabs));
	if (last) {
		wmain_action(&gg->wmain.wmain, A_LIST_NEW);
	} else {
		uint newsel = (i == 0) ? i + 1 : i - 1;
		ffui_tab_setactive(&gg->wmain.tabs, newsel);
		ffui_view_clear(&gg->wmain.vlist);
		corecmd_add(A_LIST_SEL, (void*)(size_t)newsel);
	}
	ffui_tab_del(&gg->wmain.tabs, i);
	corecmd_add(A_LIST_DEL, (void*)(size_t)i);
}

/** Add the files chosen by user */
void list_chooseaddfiles()
{
	char *fn;
	if (NULL == (fn = ffui_dlg_open(&gg->dlg, &gg->wmain.wmain)))
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
	char *fn;
	ffstr name;
	ffstr_setz(&name, "Playlist.m3u8");
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &gg->wmain.wmain, name.ptr, name.len)))
		return;

	char *list_fn;
	if (NULL == (list_fn = ffsz_alcopyz(fn)))
		return;
	corecmd_add(A_LIST_SAVE, list_fn);
}

void hidetotray()
{
	ffui_show(&gg->wmain.wmain, 0);
	if (!ffui_tray_hasicon(&gg->wmain.tray_icon)) {
		ffui_icon ico;
		char *fn = core->getpath(FFSTR("fmedia.ico"));
		ffui_icon_load(&ico, fn);
		ffmem_free(fn);
		ffui_tray_seticon(&gg->wmain.tray_icon, &ico);
	}
	ffui_tray_show(&gg->wmain.tray_icon, 1);
}

/** Load widths of list's columns. */
static void list_cols_width_load()
{
	ffui_viewcol vc = {};
	ffui_viewcol_reset(&vc);
	for (uint i = 0;  i != _H_LAST;  i++) {
		FF_ASSERT(i != FFCNT(gg->conf.list_col_width));
		if (gg->conf.list_col_width[i] == 0)
			continue;

		ffui_viewcol_setwidth(&vc, gg->conf.list_col_width[i]);
		ffui_view_setcol(&gg->wmain.vlist, i, &vc);
	}
}

/** Write widths of list's columns to config. */
void wmain_list_cols_width_write(ffconfw *conf)
{
	ffui_viewcol vc = {};
	for (uint i = 0;  i != _H_LAST;  i++) {
		ffui_viewcol_reset(&vc);
		ffui_viewcol_setwidth(&vc, 0);
		ffui_view_col(&gg->wmain.vlist, i, &vc);
		ffconf_writeint(conf, ffui_viewcol_width(&vc), 0, FFCONF_TVAL);
	}
}
