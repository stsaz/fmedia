/** fmedia: GUI: display playback properties
2020, Simon Zolin */

struct gui_wplayprops {
	ffui_wnd wplayprops;
	ffui_view vconfig;
};

const ffui_ldr_ctl wplayprops_ctls[] = {
	FFUI_LDR_CTL(struct gui_wplayprops, wplayprops),
	FFUI_LDR_CTL(struct gui_wplayprops, vconfig),
	FFUI_LDR_CTL_END
};

enum {
	CONF_RANDOM = 1,
	CONF_REPEAT,
	CONF_SEEKSTEP,
	CONF_SEEK_LEAP_DELTA,
	CONF_AUTO_ATTENUATE,
};

struct pprops_conf_ent {
	const char *name;
	uint id;
};

const char* const repeat_str[3] = { "None", "Track", "Playlist" };

struct pprops_conf_ent pprops_conf[] = {
	{ "Playback:", 0 },
	{ "  Seek Step (sec)",	CONF_SEEKSTEP },
	{ "  Seek Leap Delta (sec)",	CONF_SEEK_LEAP_DELTA },

	{ "List:", 0 },
	{ "  Random (0 | 1)",	CONF_RANDOM },
	{ "  Repeat (r/o)",	CONF_REPEAT },

	{ "Filters:", 0 },
	{ "  Auto Attenuate Ceiling (dB)",	CONF_AUTO_ATTENUATE },
};

void pprops_disp(ffui_view *v)
{
	struct ffui_view_disp *disp = &v->disp;
	const struct pprops_conf_ent *c = &pprops_conf[disp->idx];

	ffstr val;
	val.len = -1;
	char *zval = NULL;

	switch (disp->sub) {
	case 0:
		ffstr_setz(&val, c->name);
		break;

	case 1:
		switch (c->id) {
		case CONF_RANDOM:
			ffstr_setz(&val, "0");
			if (gg->conf.list_random)
				ffstr_setz(&val, "1");
			break;

		case CONF_REPEAT:
			ffstr_setz(&val, repeat_str[gg->conf.list_repeat]);
			break;

		case CONF_SEEKSTEP:
			zval = ffsz_allocfmt("%u", gg->conf.seek_step_delta);
			break;

		case CONF_SEEK_LEAP_DELTA:
			zval = ffsz_allocfmt("%u", gg->conf.seek_leap_delta);
			break;

		case CONF_AUTO_ATTENUATE:
			zval = ffsz_allocfmt("%.02F", (double)gg->conf.auto_attenuate_ceiling);
			break;
		}
	}

	if (zval != NULL)
		ffstr_setz(&val, zval);

	if (val.len != (ffsize)-1)
		disp->text.len = ffs_append(disp->text.ptr, 0, disp->text.len, val.ptr, val.len);

	ffmem_free(zval);
}

void pprops_edit()
{
	struct gui_wplayprops *w = gg->wplayprops;
	const struct pprops_conf_ent *c = &pprops_conf[w->vconfig.edited.idx];
	ffstr text = FFSTR_INITZ(w->vconfig.edited.new_text);
	int i;
	double d;
	int k = 0;

	switch (c->id) {
	case CONF_RANDOM:
		if (ffstr_to_uint32(&text, &i)) {
			gg->conf.list_random = !!i;
			k = 1;
		}
		break;

	case CONF_SEEKSTEP:
		if (ffstr_to_uint32(&text, &gg->conf.seek_step_delta))
			k = 1;
		break;

	case CONF_SEEK_LEAP_DELTA:
		if (ffstr_to_uint32(&text, &gg->conf.seek_leap_delta))
			k = 1;
		break;

	case CONF_AUTO_ATTENUATE:
		if (ffstr_to_float(&text, &d)) {
			gg->conf.auto_attenuate_ceiling = d;
			k = 1;
		}
		break;
	}

	if (k)
		ffui_view_setdata(&w->vconfig, w->vconfig.edited.idx, 0);
}

void wplayprops_action(ffui_wnd *wnd, int id)
{
	struct gui_wplayprops *w = gg->wplayprops;
	switch (id) {
	case A_PLAYPROPS_EDIT:
		pprops_edit();
		break;

	case A_PLAYPROPS_DISP:
		pprops_disp(&w->vconfig);
		break;
	}
}

void wplayprops_show(uint show)
{
	struct gui_wplayprops *w = gg->wplayprops;

	if (!show) {
		ffui_show(&w->wplayprops, 0);
		return;
	}

	ffui_view_setdata(&w->vconfig, 0, FF_COUNT(pprops_conf));
	ffui_show(&w->wplayprops, 1);
}

void wplayprops_init()
{
	struct gui_wplayprops *w = ffmem_new(struct gui_wplayprops);
	gg->wplayprops = w;
	w->wplayprops.hide_on_close = 1;
	w->wplayprops.on_action = wplayprops_action;
	w->vconfig.dispinfo_id = A_PLAYPROPS_DISP;
	w->vconfig.edit_id = A_PLAYPROPS_EDIT;
}
