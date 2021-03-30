/** fmedia: gui-winapi: playback properties
2021, Simon Zolin */

struct gui_wplayprops {
	ffui_wnd wplayprops;
	ffui_view vconfig;

	int first :1;
};

const ffui_ldr_ctl wplayprops_ctls[] = {
	FFUI_LDR_CTL(struct gui_wplayprops, wplayprops),
	FFUI_LDR_CTL(struct gui_wplayprops, vconfig),
	FFUI_LDR_CTL_END
};

const char* const repeat_str[3] = { "None", "Track", "Playlist" };

void wplayprops_action(ffui_wnd *wnd, int id)
{
}

enum {
	CONF_RANDOM = 1,
	CONF_REPEAT,
	CONF_AUTO_ATTENUATE,
};

struct conf_ent {
	const char *name;
	uint id;
};

struct conf_ent playprops_conf[] = {
	{ "List:", 0 },
		{ "  Random", CONF_RANDOM },
		{ "  Repeat", CONF_REPEAT },

	{ "Filters:", 0 },
		{ "  Auto Attenuate Ceiling (dB)", CONF_AUTO_ATTENUATE },
};

void wplayprops_fill()
{
	struct gui_wplayprops *w = gg->wplayprops;
	char *val = NULL;
	const char *cval = NULL;

	ffui_viewitem item;
	ffui_view_iteminit(&item);

	const struct conf_ent *it;
	FFARRS_FOREACH(playprops_conf, it) {
		ffui_view_settextz(&item, it->name);
		ffui_view_append(&w->vconfig, &item);

		switch (it->id) {
		case CONF_RANDOM:
			cval = (gg->list_random) ? "true" : "false";
			break;

		case CONF_REPEAT:
			cval = repeat_str[gg->conf.list_repeat];
			break;

		case CONF_AUTO_ATTENUATE:
			val = ffsz_allocfmt("%.02F"
				, (double)gg->conf.auto_attenuate_ceiling);
			cval = val;
			break;
		}

		if (cval != NULL) {
			ffui_view_settextz(&item, cval);
			ffui_view_set(&w->vconfig, 1, &item);
			cval = NULL;

			ffmem_free(val);
			val = NULL;
		}
	}
}

void wplayprops_show(uint show)
{
	struct gui_wplayprops *w = gg->wplayprops;

	if (!show) {
		ffui_show(&w->wplayprops, 0);
		return;
	}

	if (w->first) {
		w->first = 0;
		wplayprops_fill();
	}
	ffui_show(&w->wplayprops, 1);
}

void wplayprops_init()
{
	struct gui_wplayprops *w = ffmem_new(struct gui_wplayprops);
	gg->wplayprops = w;
	w->wplayprops.hide_on_close = 1;
	w->wplayprops.on_action = wplayprops_action;
	w->first = 1;
}
