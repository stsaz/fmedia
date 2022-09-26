/** fmedia: gui-winapi: filter playlist items
2021, Simon Zolin */

typedef struct gui_wfilter {
	ffui_wnd wnd;
	ffui_edit ttext;
	ffui_chbox cbfilename;
	ffui_btn breset;
	ffui_paned pntext;
} gui_wfilter;

const ffui_ldr_ctl wfilter_ctls[] = {
	FFUI_LDR_CTL(struct gui_wfilter, wnd),
	FFUI_LDR_CTL(struct gui_wfilter, ttext),
	FFUI_LDR_CTL(struct gui_wfilter, cbfilename),
	FFUI_LDR_CTL(struct gui_wfilter, breset),
	FFUI_LDR_CTL(struct gui_wfilter, pntext),
	FFUI_LDR_CTL_END
};

void filt_apply(void *param)
{
	struct gui_wfilter *w = gg->wfilter;
	ffstr s;
	ffui_textstr(&w->ttext, &s);
	uint flags = GUI_FILT_META;
	if (ffui_chbox_checked(&w->cbfilename))
		flags |= GUI_FILT_URL;
	gui_filter(&s, flags);
	ffstr_free(&s);
}

void wfilter_action(ffui_wnd *wnd, int id)
{
	struct gui_wfilter *w = gg->wfilter;

	switch (id) {
	case FILTER_APPLY:
		corecmd_addfunc(filt_apply, NULL);
		break;
	case FILTER_RESET:
		ffui_settextz(&w->ttext, "");
		break;
	}
}

void wfilter_show(uint show)
{
	struct gui_wfilter *w = gg->wfilter;

	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}

	ffui_edit_selall(&w->ttext);
	ffui_setfocus(&w->ttext);
	ffui_show(&w->wnd, 1);
}

void wfilter_init()
{
	struct gui_wfilter *w = ffmem_new(struct gui_wfilter);
	gg->wfilter = w;
	w->wnd.hide_on_close = 1;
	w->wnd.on_action = wfilter_action;
}
