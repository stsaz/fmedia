/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/path.h>


static void wmain_action(ffui_wnd *wnd, int id);
static void list_save();
static void hidetotray();


void wmain_init()
{
	ffui_trk_set(&gg->wmain.tvol, 100);
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
		ffstr *s = ffmem_new(ffstr);
		ffstr_alcopyz(s, fn);
		corecmd_add(A_URL_ADD, s);
		return;
	}
	case A_LIST_ADDURL:
		ffui_show(&gg->wuri.wuri, 1);
		return;

	case A_SHOWPCM:
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

void wmain_ent_added(void *param)
{
	uint idx = (size_t)param;
	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, idx);
	char buf[256];
	ffstr *val = NULL, s;

	ffui_viewitem it = {};
	ffs_fmt(buf, buf + sizeof(buf), "%u%Z", idx + 1);
	ffui_view_settextz(&it, buf);
	ffui_view_ins(&gg->wmain.vlist, idx, &it);

	for (uint i = H_IDX + 1;  i != H_FN + 1;  i++) {

		val = gg->qu->meta_find(ent, list_colname[i], ffsz_len(list_colname[i]));

		switch (i) {
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

		case H_ART:
		case H_INF:
		case H_DATE:
		case H_ALBUM:
			break;

		case H_FN:
			val = &ent->url;
			break;
		}

		if (val == NULL)
			continue;

		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->wmain.vlist, i, &it);
	}

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);
}

void wmain_ent_removed(uint idx)
{
	ffui_viewitem it = {};
	ffui_view_setindex(&it, idx);
	ffui_send_view_rm(&gg->wmain.vlist, &it);
}

void wmain_newtrack(fmed_que_entry *ent, uint time_total)
{
	ffui_post_trk_setrange(&gg->wmain.tpos, time_total);

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
