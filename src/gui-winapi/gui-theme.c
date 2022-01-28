/** GUI themes.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>
#include <gui-winapi/gui.h>
#include <FF/gui/loader.h>
#include <FF/data/conf-copy.h>
#include <FFOS/process.h>


#define dbglog0(...)  fmed_dbglog(core, NULL, "gui", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "gui", __VA_ARGS__)
#define syserrlog0(...)  fmed_syserrlog(core, NULL, "gui", __VA_ARGS__)

enum {
	MENU_FIRST_IDX = 11,
};

struct theme {
	char *name;
};

struct theme_reader {
	struct theme *find;
	uint found :1;
};

static int theme_conf_new(ffconf_scheme *cs, void *obj)
{
	struct theme_reader *tr = obj;
	const ffstr *name = ffconf_scheme_objval(cs);

	static const ffconf_arg dummy = {};

	if (tr->find != NULL) {
		if (!ffstr_eqz(name, tr->find->name)) {
			ffconf_scheme_skipctx(cs);
			return 0;
		}
		ffconf_scheme_addctx(cs, &dummy, NULL);
		tr->found = 1;
		return 0;
	}

	struct theme *t = ffarr_pushgrowT(&gg->themes, 2, struct theme);
	if (NULL == (t->name = ffsz_alcopystr(name)))
		return FFCONF_ESYS;
	ffconf_scheme_skipctx(cs);
	return 0;
}

static const ffconf_arg gui_themeconf_args[] = {
	{ "theme",	FFCONF_TOBJ | FFCONF_FNOTEMPTY | FFCONF_FMULTI, (ffsize)theme_conf_new },
	{}
};

/** Read themes data from file and:
 a) prepare themes array (set theme name)
 b) gather theme's data and write to file */
static int themes_read2(struct theme *t, const char *ofn)
{
	int rc = -1, r;
	ffbool copy = 0;
	ffconf_ctxcopy ctxcopy = {};
	ffstr themedata = {};
	ffvec ddata = {};
	char *fn = NULL;

	struct theme_reader tr = {};
	tr.find = t;

	ffconf conf = {};
	ffconf_init(&conf);

	ffconf_scheme cs = {};
	ffconf_scheme_init(&cs, &conf);
	ffconf_scheme_addctx(&cs, gui_themeconf_args, &tr);

	fn = core->getpath(FFSTR("theme.conf"));
	if (0 != fffile_readwhole(fn, &ddata, -1)) {
		syserrlog0("theme.conf parser: fffile_readwhole");
		goto end;
	}

	ffstr data;
	ffstr_set2(&data, &ddata);

	while (data.len != 0) {
		r = ffconf_parse(&conf, &data);
		if (r < 0) {
			errlog0("theme.conf: ffconf_parse: %s", ffconf_errstr(r));
			goto end;
		}

		if (copy) {
			r = ffconf_ctx_copy(&ctxcopy, conf.val, r);
			if (r < 0) {
				errlog0("theme.conf parser: ffconf_ctx_copy");
				goto end;
			} else if (r > 0) {
				themedata = ffconf_ctxcopy_acquire(&ctxcopy);
				ffconf_ctxcopy_destroy(&ctxcopy);
				if (0 != fffile_writewhole(ofn, themedata.ptr, themedata.len, 0)) {
					syserrlog0("theme.conf parser: fffile_writewhole(): %s", ofn);
					goto end;
				}
				rc = 0;
				goto end;
			}
			continue;
		}

		r = ffconf_scheme_process(&cs, r);
		if (r < 0) {
			errlog0("theme.conf: ffconf_scheme_process: %s: %s", ffconf_errstr(r), cs.errmsg);
			goto end;
		}

		if (tr.found) {
			ffconf_ctxcopy_init(&ctxcopy);
			copy = 1;
		}
	}

end:
	ffmem_free(fn);
	ffstr_free(&themedata);
	ffconf_fin(&conf);
	ffconf_scheme_destroy(&cs);
	ffvec_free(&ddata);
	ffconf_ctxcopy_destroy(&ctxcopy);
	return rc;
}

void gui_themes_read(void)
{
	themes_read2(NULL, NULL);
}

void gui_themes_destroy(void)
{
	struct theme *t;
	FFARR_WALKT(&gg->themes, t, struct theme) {
		ffmem_free(t->name);
	}
	ffarr_free(&gg->themes);
}

/** Add menu items to switch between themes. */
void gui_themes_add(uint def)
{
	struct theme *t;
	ffui_menuitem mi = {};
	char buf[64];
	uint i = MENU_FIRST_IDX, id = 0;

	ffui_menu_rm(&gg->mfile, i);

	if (def == 0)
		ffui_menu_addstate(&mi, FFUI_MENU_CHECKED);

	FFARR_WALKT(&gg->themes, t, struct theme) {
		ffui_menu_setcmd(&mi, (id++ << 8) | SETTHEME);
		ffui_menu_settype(&mi, FFUI_MENU_RADIOCHECK);
		size_t n = ffs_fmt(buf, buf + sizeof(buf), "Theme: %s", t->name);
		ffui_menu_settext(&mi, buf, n);
		ffui_menu_ins(&gg->mfile, i++, &mi);
	}

	if (def != 0)
		gui_theme_set(def);
}

/** Apply UI changes from a theme. */
void gui_theme_set(int idx)
{
	dbglog0("applying theme %d...", idx);
	struct theme *t = ffarr_itemT(&gg->themes, idx, struct theme);

	char *fn = ffenv_expand(NULL, NULL, 0, "%TMP%\\fmedia-theme.conf");
	if (0 != themes_read2(t, fn))
		return;

	ffui_loader ldr;
	ffui_ldr_init2(&ldr, &gui_getctl, NULL, gg);
	ffui_ldr_loadconf(&ldr, fn);
	fffile_rm(fn);

	ffui_menuitem mi = {};
	ffui_menu_clearstate(&mi, FFUI_MENU_CHECKED);
	ffui_menu_set(&gg->mfile, MENU_FIRST_IDX + gg->theme_index, &mi);
	ffui_menu_addstate(&mi, FFUI_MENU_CHECKED);
	ffui_menu_set(&gg->mfile, MENU_FIRST_IDX + idx, &mi);
	gg->theme_index = idx;

	wmain_redraw();

	ffmem_free(fn);
	ffui_ldr_fin(&ldr);
}
