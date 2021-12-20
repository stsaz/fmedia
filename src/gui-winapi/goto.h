/** fmedia: gui-winapi: go to audio position
2021, Simon Zolin */

struct gui_wgoto {
	ffui_wnd wgoto;
	ffui_edit etime;
	ffui_btn bgo;
};

const ffui_ldr_ctl wgoto_ctls[] = {
	FFUI_LDR_CTL(struct gui_wgoto, wgoto),
	FFUI_LDR_CTL(struct gui_wgoto, etime),
	FFUI_LDR_CTL(struct gui_wgoto, bgo),
	FFUI_LDR_CTL_END
};

void wgoto_action(ffui_wnd *wnd, int id)
{
	struct gui_wgoto *w = gg->wgoto;

	switch (id) {
	case A_PLAY_GOTO: {
		ffstr s;
		ffdatetime dt;
		fftime t;

		ffui_textstr(&w->etime, &s);
		if (s.len != fftime_fromstr1(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
			return;

		fftime_join1(&t, &dt);
		gtrk_seek2(fftime_sec(&t));
		ffui_show(&w->wgoto, 0);
		break;
	}
	}
}

void wgoto_show(uint show, uint pos)
{
	struct gui_wgoto *w = gg->wgoto;

	if (!show) {
		ffui_show(&w->wgoto, 0);
		return;
	}

	ffarr s = {};
	ffstr_catfmt(&s, "%02u:%02u", pos / 60, pos % 60);
	ffui_settextstr(&w->etime, &s);
	ffarr_free(&s);
	ffui_edit_selall(&w->etime);
	ffui_setfocus(&w->etime);
	ffui_show(&w->wgoto, 1);
}

void wgoto_init()
{
	struct gui_wgoto *w = ffmem_new(struct gui_wgoto);
	gg->wgoto = w;
	w->wgoto.hide_on_close = 1;
	w->wgoto.on_action = wgoto_action;
}
