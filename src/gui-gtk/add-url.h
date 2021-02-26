/** fmedia: gui-gtk: add URL
2021, Simon Zolin */

struct gui_wuri {
	ffui_wnd wuri;
	ffui_edit turi;
	ffui_btn bok;
};

const ffui_ldr_ctl wuri_ctls[] = {
	FFUI_LDR_CTL(struct gui_wuri, wuri),
	FFUI_LDR_CTL(struct gui_wuri, turi),
	FFUI_LDR_CTL(struct gui_wuri, bok),
	FFUI_LDR_CTL_END
};

void wuri_action(ffui_wnd *wnd, int id)
{
	struct gui_wuri *w = gg->wuri;
	dbglog("%s cmd:%u", __func__, id);

	switch (id) {
	case A_URL_ADD: {
		ffstr *s = ffmem_new(ffstr);
		ffui_edit_textstr(&w->turi, s);
		if (s->len != 0)
			corecmd_add(A_URL_ADD, s);
		else {
			ffstr_free(s);
			ffmem_free(s);
		}
		ffui_show(&w->wuri, 0);
		break;
	}
	}
}

void wuri_show(uint show)
{
	struct gui_wuri *w = gg->wuri;

	if (!show) {
		ffui_show(&w->wuri, 0);
		return;
	}

	ffui_show(&w->wuri, 1);
}

void wuri_init()
{
	struct gui_wuri *w = ffmem_new(struct gui_wuri);
	gg->wuri = w;
	w->wuri.hide_on_close = 1;
	w->wuri.on_action = &wuri_action;
}
