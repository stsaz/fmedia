/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>
#include <gui-gtk/convert.h>
#include <gui-gtk/log.h>
#include <gui-gtk/meta-info.h>
#include <gui-gtk/play-props.h>
#include <gui-gtk/youtube-dload.h>


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
