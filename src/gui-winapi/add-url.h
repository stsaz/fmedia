/** fmedia: gui-winapi: add URL
2021, Simon Zolin */

struct gui_wuri {
	ffui_wnd wuri;
	ffui_edit turi;
	ffui_btn bok;
	ffui_btn bcancel;
	ffui_paned pnuri;
};

const ffui_ldr_ctl wuri_ctls[] = {
	FFUI_LDR_CTL(struct gui_wuri, wuri),
	FFUI_LDR_CTL(struct gui_wuri, turi),
	FFUI_LDR_CTL(struct gui_wuri, bok),
	FFUI_LDR_CTL(struct gui_wuri, bcancel),
	FFUI_LDR_CTL(struct gui_wuri, pnuri),
	FFUI_LDR_CTL_END
};

void url_add(void *param)
{
	struct gui_wuri *w = gg->wuri;
	ffstr s;
	ffui_textstr(&w->turi, &s);
	if (s.len != 0)
		gui_media_add2(s.ptr, -1, 0);
	ffstr_free(&s);
	ffui_show(&w->wuri, 0);
}

void wuri_action(ffui_wnd *wnd, int id)
{
	struct gui_wuri *w = gg->wuri;

	switch (id) {
	case URL_ADD:
		corecmd_addfunc(url_add, NULL);
		break;
	case URL_CLOSE:
		ffui_show(&w->wuri, 0);
		break;
	}
}

void wuri_show(uint show)
{
	struct gui_wuri *w = gg->wuri;

	if (!show) {
		ffui_show(&w->wuri, 0);
		return;
	}

	ffui_edit_selall(&w->turi);
	ffui_setfocus(&w->turi);
	ffui_show(&w->wuri, 1);
}

void wuri_init()
{
	struct gui_wuri *w = ffmem_new(struct gui_wuri);
	gg->wuri = w;
	w->wuri.hide_on_close = 1;
	w->wuri.on_action = wuri_action;
}
