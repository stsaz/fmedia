/** fmedia: gui-winapi: about
2021, Simon Zolin */

struct gui_wabout {
	ffui_wnd wabout;
	ffui_img ico;
	ffui_label labout;
	ffui_label lurl;
};

const ffui_ldr_ctl wabout_ctls[] = {
	FFUI_LDR_CTL(struct gui_wabout, wabout),
	FFUI_LDR_CTL(struct gui_wabout, ico),
	FFUI_LDR_CTL(struct gui_wabout, labout),
	FFUI_LDR_CTL(struct gui_wabout, lurl),
	FFUI_LDR_CTL_END
};

void wabout_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case OPEN_HOMEPAGE:
		if (0 != ffui_shellexec(FMED_HOMEPAGE, SW_SHOWNORMAL))
			syserrlog0("ShellExecute()");
		break;
	}
}

void wabout_show(uint show)
{
	struct gui_wabout *w = gg->wabout;

	if (!show) {
		ffui_show(&w->wabout, 0);
		return;
	}

	char buf[255];
	ffs_format(buf, sizeof(buf),
		"fmedia v%s\n\n"
		"Fast media player, recorder, converter%Z"
		, core->props->version_str);
	ffui_settextz(&w->labout, buf);

	ffui_settextz(&w->lurl, FMED_HOMEPAGE);
	ffui_show(&w->wabout, 1);
}

void wabout_init()
{
	struct gui_wabout *w = ffmem_new(struct gui_wabout);
	gg->wabout = w;
	w->wabout.hide_on_close = 1;
	w->wabout.on_action = wabout_action;
}
