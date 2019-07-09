/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/path.h>


static void wmain_action(ffui_wnd *wnd, int id);
static void list_save();
static void hidetotray();
static void list_dispinfo(struct ffui_view_disp *disp);
static void list_cols_width_load();


void wmain_init()
{
	gg->wmain.vlist.dispinfo_id = LIST_DISPINFO;
	ffui_trk_set(&gg->wmain.tvol, 100);

	list_cols_width_load();

	ffui_dlg_multisel(&gg->dlg, 1);
	gg->wmain.wmain.on_action = &wmain_action;
	gg->wmain.wmain.onclose_id = A_ONCLOSE;
	ffui_view_dragdrop(&gg->wmain.vlist, A_ONDROPFILE);
	ffui_show(&gg->wmain, 1);
}

static void wmain_action(ffui_wnd *wnd, int id)
{
	dbglog("%s cmd:%u", __func__, id);

	switch (id) {
	case A_LIST_ADDFILE: {
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
		return;
	}
	case A_LIST_ADDURL:
		ffui_show(&gg->wuri.wuri, 1);
		return;

	case A_SHOWPCM:
	case A_SHOWDIR:
	case A_DELFILE:
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
	case A_STOP:
	case A_STOP_AFTER:
	case A_NEXT:
	case A_PREV:
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

	case A_LIST_SAVE:
		list_save();
		return;
	case A_LIST_SELECTALL:
		ffui_view_selall(&gg->wmain.vlist);
		return;
	case A_LIST_REMOVE:
	case A_LIST_RMDEAD:
	case A_LIST_CLEAR:
	case A_LIST_RANDOM:
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

	case A_CONF_EDIT:
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
		ffs_fmt(buf, buf + sizeof(buf), "%u%Z", disp->idx + 1);
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
	ffui_post_trk_setrange(&gg->wmain.tpos, time_total);
	ent->dur = time_total * 1000;

	char buf[1024];
	size_t n;

	n = ffs_fmt(buf, buf + sizeof(buf), "%u kbps, %s, %u Hz, %s, %s"
		, (d->audio.bitrate + 500) / 1000
		, d->audio.decoder
		, d->audio.fmt.sample_rate
		, ffpcm_fmtstr(d->audio.fmt.format)
		, ffpcm_channelstr(d->audio.fmt.channels));
	gg->qu->meta_set(ent, FFSTR("__info"), buf, n, FMED_QUE_PRIV | FMED_QUE_OVWRITE);

	uint idx = gg->qu->cmdv(FMED_QUE_ID, ent);
	ffui_send_view_setdata(&gg->wmain.vlist, idx, 0);

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
