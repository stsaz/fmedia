/** fmedia: GUI: display playback properties
2020, Simon Zolin */

static void wplayprops_action(ffui_wnd *wnd, int id)
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

const char* const repeat_str[3] = { "None", "Track", "Playlist" };

static struct conf_ent conf[] = {
	{ "List:", 0 },
		{ "  Random", CONF_RANDOM },
		{ "  Repeat", CONF_REPEAT },

	{ "Filters:", 0 },
		{ "  Auto Attenuate Ceiling (dB)", CONF_AUTO_ATTENUATE },
};

static void wplayprops_fill()
{
	char *val = NULL;
	const char *cval = NULL;

	ffui_viewitem item;
	ffui_view_iteminit(&item);

	const struct conf_ent *it;
	FFARRS_FOREACH(conf, it) {
		ffui_view_settextz(&item, it->name);
		ffui_view_append(&gg->wplayprops.vconfig, &item);

		switch (it->id) {
		case CONF_RANDOM:
			cval = (gg->conf.list_random) ? "true" : "false";
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
			ffui_view_set(&gg->wplayprops.vconfig, 1, &item);
			cval = NULL;

			ffmem_free(val);
			val = NULL;
		}
	}
}

void wplayprops_init()
{
	gg->wplayprops.wplayprops.hide_on_close = 1;
	gg->wplayprops.wplayprops.on_action = wplayprops_action;
	wplayprops_fill();
}
