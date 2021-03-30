/** GUI themes.
Copyright (c) 2018 Simon Zolin */

#include <fmedia.h>
#include <gui-winapi/gui.h>
#include <FF/gui/loader.h>
#include <FF/data/conf.h>
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

static int theme_conf_new(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	struct theme_reader *tr = obj;
	const ffstr *name = ffpars_ctxname(p);

	if (tr->find != NULL) {
		if (!ffstr_eqz(name, tr->find->name)) {
			ffpars_ctx_skip(ctx);
			return 0;
		}
		tr->found = 1;
		return 0;
	}

	struct theme *t = ffarr_pushgrowT(&gg->themes, 2, struct theme);
	if (NULL == (t->name = ffsz_alcopystr(name)))
		return FFPARS_ESYS;
	ffpars_ctx_skip(ctx);
	return 0;
}

static const ffpars_arg gui_themeconf_args[] = {
	{ "theme",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&theme_conf_new) },
};

/** Read themes data from file and:
 a) prepare themes array (set theme name)
 b) gather theme's data and write to file */
static void themes_read2(struct theme *t, const char *ofn)
{
	int r;
	struct theme_reader tr = {};
	ffbool copy = 0;
	ffparser_schem ps;
	ffconf conf;
	ffpars_ctx ctx;
	ffconf_ctxcopy ctxcopy = {};
	ffstr themedata = {};
	ffarr ddata = {};
	char *fn = NULL;

	tr.find = t;

	ffpars_setargs(&ctx, &tr, gui_themeconf_args, FFCNT(gui_themeconf_args));
	if (0 != (r = ffconf_scheminit(&ps, &conf, &ctx)))
		goto fail;

	fn = core->getpath(FFSTR("theme.conf"));
	if (0 != fffile_readall(&ddata, fn, -1)) {
		r = FFPARS_ESYS;
		goto fail;
	}

	ffstr data;
	ffstr_set2(&data, &ddata);

	while (data.len != 0) {
		r = ffconf_parsestr(&conf, &data);
		if (ffpars_iserr(r))
			goto fail;

		if (copy) {
			r = ffconf_ctx_copy(&ctxcopy, &conf);
			if (r < 0) {
				r = FFPARS_EINTL;
				goto fail;
			} else if (r > 0) {
				themedata = ffconf_ctxcopy_acquire(&ctxcopy);
				ffconf_ctxcopy_destroy(&ctxcopy);
				if (0 != fffile_writeall(ofn, themedata.ptr, themedata.len, 0)) {
					syserrlog0("fffile_writeall(): %s", ofn);
					goto fail;
				}
				r = 0;
				goto fail;
			}
			continue;
		}

		r = ffconf_schemrun(&ps);
		if (ffpars_iserr(r))
			goto fail;

		if (tr.found) {
			ffconf_ctxcopy_init(&ctxcopy, &ps);
			copy = 1;
		}
	}

	r = ffconf_schemfin(&ps);
	if (ffpars_iserr(r))
		goto fail;

fail:
	if (ffpars_iserr(r))
		errlog0("theme.conf parser: %s"
			, ffpars_errstr(r));

	ffmem_free(fn);
	ffstr_free(&themedata);
	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);
	ffarr_free(&ddata);
	ffconf_ctxcopy_destroy(&ctxcopy);
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
	themes_read2(t, fn);

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
