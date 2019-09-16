/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <FF/time.h>


static void wabout_action(ffui_wnd *wnd, int id)
{
}

void wabout_init()
{
	char buf[255];
	int r = ffs_fmt(buf, buf + sizeof(buf),
		"fmedia v%s\n\n"
		"Fast media player, recorder, converter"
		, core->props->version_str);
	ffui_lbl_settext(&gg->wabout.labout, buf, r);
	ffui_lbl_settextz(&gg->wabout.lurl, FMED_HOMEPAGE);
	gg->wabout.wabout.hide_on_close = 1;
	gg->wabout.wabout.on_action = &wabout_action;
}


static void wuri_action(ffui_wnd *wnd, int id)
{
	dbglog("%s cmd:%u", __func__, id);

	switch (id) {
	case A_URL_ADD: {
		ffstr *s = ffmem_new(ffstr);
		ffui_edit_textstr(&gg->wuri.turi, s);
		if (s->len != 0)
			corecmd_add(A_URL_ADD, s);
		else {
			ffstr_free(s);
			ffmem_free(s);
		}
		ffui_show(&gg->wuri.wuri, 0);
		break;
	}
	}
}

void wuri_init()
{
	gg->wuri.wuri.hide_on_close = 1;
	gg->wuri.wuri.on_action = &wuri_action;
}


static void winfo_action(ffui_wnd *wnd, int id)
{
}

void winfo_init()
{
	gg->winfo.winfo.hide_on_close = 1;
	gg->winfo.winfo.on_action = &winfo_action;
}

static void winfo_addpair(const ffstr *name, const ffstr *val)
{
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffui_view_settextstr(&it, name);
	ffui_view_append(&gg->winfo.vinfo, &it);

	ffui_view_settextstr(&it, val);
	ffui_view_set(&gg->winfo.vinfo, 1, &it);
}

/** Show info about a playlist's item. */
void winfo_show(uint idx)
{
	ffstr name, empty = {}, *val;
	ffarr data = {};
	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, idx);
	if (ent == NULL)
		return;

	ffui_wnd_settextz(&gg->winfo.winfo, ent->url.ptr);
	ffui_view_clear(&gg->winfo.vinfo);

	ffstr_setz(&name, "File path");
	winfo_addpair(&name, &ent->url);

	fffileinfo fi = {};
	ffbool have_fi = (0 == fffile_infofn(ent->url.ptr, &fi));
	ffarr_alloc(&data, 255);

	ffstr_setz(&name, "File size");
	data.len = 0;
	if (have_fi)
		ffstr_catfmt(&data, "%U KB", fffile_infosize(&fi) / 1024);
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "File date");
	data.len = 0;
	if (have_fi) {
		ffdtm dt;
		fftime t = fffile_infomtime(&fi);
		fftime_split(&dt, &t, FFTIME_TZLOCAL);
		data.len = fftime_tostr(&dt, data.ptr, data.cap, FFTIME_DATE_WDMY | FFTIME_HMS);
	}
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "Info");
	if (NULL == (val = gg->qu->meta_find(ent, FFSTR("__info"))))
		val = &empty;
	winfo_addpair(&name, val);

	for (uint i = 0;  NULL != (val = gg->qu->meta(ent, i, &name, 0));  i++) {

		if (val == FMED_QUE_SKIP)
			continue;

		winfo_addpair(&name, val);
	}

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);

	ffui_show(&gg->winfo.winfo, 1);

	ffarr_free(&data);
}
