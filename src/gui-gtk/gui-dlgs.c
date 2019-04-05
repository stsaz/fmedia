/**
Copyright (c) 2019 Simon Zolin */

#include <gui-gtk/gui.h>


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
