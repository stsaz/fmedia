/**
Copyright (c) 2019 Simon Zolin
*/

#include "loader.h"
#include <FFOS/path.h>
#include <FFOS/file.h>



#define T_STR  FFCONF_TSTR
#define T_STRMULTI  (FFCONF_TSTR | FFCONF_FMULTI)
#define T_CLOSE  FFCONF_TCLOSE
#define T_OBJ  FFCONF_TOBJ
#define T_INT32  FFCONF_TINT32
#define T_STRLIST  (FFCONF_TSTR | FFCONF_FLIST)
#define T_OBJMULTI (FFCONF_TOBJ | FFCONF_FMULTI)

void* ffui_ldr_findctl(const ffui_ldr_ctl *ctx, void *ctl, const ffstr *name)
{
	ffstr s = *name, sctl;

	while (s.len != 0) {
		ffstr_splitby(&s, '.', &sctl, &s);
		for (uint i = 0; ; i++) {
			if (ctx[i].name == NULL)
				return NULL;

			if (ffstr_eqz(&sctl, ctx[i].name)) {
				uint off = ctx[i].flags & ~0x80000000;
				ctl = (char*)ctl + off;
				if (ctx[i].flags & 0x80000000)
					ctl = *(void**)ctl;
				if (s.len == 0)
					return ctl;

				if (ctx[i].children == NULL)
					return NULL;
				ctx = ctx[i].children;
				break;
			}
		}
	}
	return NULL;
}

static void* ldr_getctl(ffui_loader *g, const ffstr *name)
{
	char buf[255];
	ffstr s;
	s.ptr = buf;
	s.len = ffs_format_r0(buf, sizeof(buf), "%s.%S", g->wndname, name);
	FFDBG_PRINTLN(10, "%S", &s);
	return g->getctl(g->udata, &s);
}


// ICON
// MENU ITEM
// MENU
// LABEL
// IMAGE
// BUTTON
// CHECKBOX
// EDITBOX
// TEXT
// TRACKBAR
// TAB
// VIEW COL
// VIEW
// STATUSBAR
// TRAYICON
// DIALOG
// WINDOW

// ICON
static const ffconf_arg icon_args[] = {
	{ "filename",	FFCONF_TSTRZ, FF_OFF(_ffui_ldr_icon, fn) },
	{}
};


// MENU ITEM
static int mi_submenu(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_menu *sub;

	if (NULL == (sub = g->getctl(g->udata, val)))
		return FFCONF_EBADVAL;

	ffui_menu_setsubmenu(g->mi, sub, g->wnd);
	return 0;
}

static int mi_action(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id;

	if (0 == (id = g->getcmd(g->udata, val)))
		return FFCONF_EBADVAL;

	ffui_menu_setcmd(g->mi, id);
	return 0;
}

static int mi_hotkey(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_wnd_hotkey *a;
	uint hk;

	if (NULL == (a = ffvec_pushT(&g->accels, ffui_wnd_hotkey)))
		return FFCONF_ESYS;

	if (0 == (hk = ffui_hotkey_parse(val->ptr, val->len)))
		return FFCONF_EBADVAL;

	a->hk = hk;
	a->h = g->mi;
	return 0;
}

static int mi_done(ffconf_scheme *cs, ffui_loader *g)
{
	ffui_menu_ins(g->menu, g->mi, -1);
	return 0;
}

static const ffconf_arg mi_args[] = {
	{ "submenu",	T_STR, (ffsize)mi_submenu },
	{ "action",	T_STR, (ffsize)mi_action },
	{ "hotkey",	T_STR, (ffsize)mi_hotkey },
	{ NULL,	T_CLOSE, (ffsize)mi_done },
};

static int mi_new(ffconf_scheme *cs, ffui_loader *g)
{
	const ffstr *name = ffconf_scheme_objval(cs);

	if (ffstr_eqcz(name, "-"))
		g->mi = ffui_menu_newsep();
	else {
		ffstr s = vars_val(&g->vars, *name);
		char *sz = ffsz_dupstr(&s);
		g->mi = ffui_menu_new(sz);
		ffmem_free(sz);
	}

	ffconf_scheme_addctx(cs, mi_args, g);
	return 0;
}


// MENU
static const ffconf_arg menu_args[] = {
	{ "item",	T_OBJMULTI, (ffsize)mi_new },
	{}
};

static int menu_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->menu = g->getctl(g->udata, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;

	if (0 != ffui_menu_create(g->menu))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, menu_args, g);
	return 0;
}

static int mmenu_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->menu = ldr_getctl(g, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;

	if (0 != ffui_menu_createmain(g->menu))
		return FFCONF_ESYS;

	ffui_wnd_setmenu(g->wnd, g->menu);
	ffconf_scheme_addctx(cs, menu_args, g);
	return 0;
}


// LABEL
static int lbl_style(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	if (ffstr_eqcz(val, "horizontal"))
		g->f_horiz = 1;
	else
		return FFCONF_EBADVAL;
	return 0;
}
static int lbl_text(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_lbl_settextstr(g->lbl, val);
	return 0;
}
static int lbl_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->f_horiz) {
		if (g->hbox == NULL) {
			g->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->hbox, /*expand=*/0, /*fill=*/0, /*padding=*/0);
		}
		gtk_box_pack_start(GTK_BOX(g->hbox), g->lbl->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	} else {
		g->hbox = NULL;
		gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->lbl->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	}
	return 0;
}
static const ffconf_arg lbl_args[] = {
	{ "style",	T_STRLIST, (ffsize)lbl_style },
	{ "text",	T_STR, (ffsize)lbl_text },
	{ NULL,	T_CLOSE, (ffsize)lbl_done },
};

static int lbl_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_lbl_create(g->lbl, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, lbl_args, g);
	g->flags = 0;
	return 0;
}


// IMAGE
static const ffconf_arg img_args[] = {
	{ NULL,	T_CLOSE, (ffsize)lbl_done },
};
static int img_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_image_create(g->img, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, img_args, g);
	g->flags = 0;
	return 0;
}


// BUTTON
static int btn_text(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_btn_settextstr(g->btn, val);
	return 0;
}
static int btn_style(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	if (ffstr_eqcz(val, "horizontal"))
		g->f_horiz = 1;
	else
		return FFCONF_EBADVAL;
	return 0;
}
static int btn_action(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFCONF_EBADVAL;

	g->btn->action_id = id;
	return 0;
}
static int btn_icon(ffconf_scheme *cs, ffui_loader *g)
{
	ffmem_tzero(&g->ico_ctl);
	ffconf_scheme_addctx(cs, icon_args, &g->ico_ctl);
	return 0;
}
static int btn_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->f_horiz) {
		if (g->hbox == NULL) {
			g->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->hbox, /*expand=*/0, /*fill=*/0, /*padding=*/0);
		}
		gtk_box_pack_start(GTK_BOX(g->hbox), g->btn->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	} else {
		g->hbox = NULL;
		gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->btn->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	}

	if (g->ico_ctl.fn != NULL) {
		ffui_icon ico;
		char *sz = ffsz_allocfmt("%S/%s", &g->path, g->ico_ctl.fn);
		ffui_icon_load(&ico, sz);
		ffmem_free(sz);
		ffui_btn_seticon(g->btn, &ico);
		ffmem_free(g->ico_ctl.fn);
		ffmem_tzero(&g->ico_ctl);
	}
	return 0;
}
static const ffconf_arg btn_args[] = {
	{ "style",	T_STRLIST, (ffsize)btn_style },
	{ "action",	T_STR, (ffsize)btn_action },
	{ "text",	T_STR, (ffsize)btn_text },
	{ "icon",	T_OBJ, (ffsize)btn_icon },
	{ NULL,	T_CLOSE, (ffsize)btn_done },
};

static int btn_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_btn_create(g->btn, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, btn_args, g);
	g->flags = 0;
	return 0;
}

// CHECKBOX
static int chbox_text(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_checkbox_settextstr(g->cb, val);
	return 0;
}
static int chbox_action(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFCONF_EBADVAL;

	g->cb->action_id = id;
	return 0;
}
static const ffconf_arg chbox_args[] = {
	{ "style",	T_STRLIST, (ffsize)btn_style },
	{ "action",	T_STR, (ffsize)chbox_action },
	{ "text",	T_STR, (ffsize)chbox_text },
	{ NULL,	T_CLOSE, (ffsize)btn_done },
};

static int chbox_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_checkbox_create(g->cb, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, chbox_args, g);
	g->flags = 0;
	return 0;
}


// EDITBOX
static int edit_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->f_horiz) {
		if (g->hbox == NULL) {
			g->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->hbox, /*expand=*/0, /*fill=*/0, /*padding=*/0);
		}
		gtk_box_pack_start(GTK_BOX(g->hbox), g->edit->h, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	} else {
		g->hbox = NULL;
		gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->edit->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	}

	return 0;
}
static int edit_text(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffui_edit_settextstr(g->edit, val);
	return 0;
}
static int edit_onchange(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFCONF_EBADVAL;

	g->edit->change_id = id;
	return 0;
}
static const ffconf_arg edit_args[] = {
	{ "style",	T_STRLIST, (ffsize)btn_style },
	{ "text",	T_STR, (ffsize)edit_text },
	{ "onchange",	T_STR, (ffsize)edit_onchange },
	{ NULL,	T_CLOSE, (ffsize)edit_done },
};

static int edit_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_edit_create(g->edit, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, edit_args, g);
	g->flags = 0;
	return 0;
}


// TEXT
static int text_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->f_horiz) {
		if (g->hbox == NULL) {
			g->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->hbox, /*expand=*/1, /*fill=*/0, /*padding=*/0);
		}
		gtk_box_pack_start(GTK_BOX(g->hbox), g->text->h, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	} else {
		void *scrl = gtk_scrolled_window_new(NULL, NULL);
		gtk_container_add(GTK_CONTAINER(scrl), g->text->h);
		gtk_scrolled_window_set_policy(scrl, GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);
		g->hbox = NULL;
		gtk_box_pack_start(GTK_BOX(g->wnd->vbox), scrl, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	}

	return 0;
}
static const ffconf_arg text_args[] = {
	{ "style",	T_STRLIST, (ffsize)btn_style },
	{ NULL,	T_CLOSE, (ffsize)text_done },
};

static int text_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_text_create(g->text, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, text_args, g);
	g->flags = 0;
	return 0;
}


// TRACKBAR
static int trkbar_range(ffconf_scheme *cs, ffui_loader *g, ffint64 val)
{
	ffui_trk_setrange(g->trkbar, val);
	return 0;
}
static int trkbar_style(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	if (ffstr_eqcz(val, "horizontal"))
		g->f_horiz = 1;
	else
		return FFCONF_EBADVAL;
	return 0;
}
static int trkbar_val(ffconf_scheme *cs, ffui_loader *g, ffint64 val)
{
	ffui_trk_set(g->trkbar, val);
	return 0;
}
static int trkbar_onscroll(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	if (0 == (g->trkbar->scroll_id = g->getcmd(g->udata, val)))
		return FFCONF_EBADVAL;
	return 0;
}
static int trkbar_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->f_loadconf)
		return 0;
	if (g->f_horiz) {
		if (g->hbox == NULL) {
			g->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->hbox, /*expand=*/0, /*fill=*/0, /*padding=*/0);
		}
		gtk_box_pack_start(GTK_BOX(g->hbox), g->trkbar->h, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	} else {
		g->hbox = NULL;
		gtk_box_pack_start(GTK_BOX(g->wnd->vbox), g->trkbar->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	}
	return 0;
}
static const ffconf_arg trkbar_args[] = {
	{ "style",	T_STRLIST, (ffsize)trkbar_style },
	{ "range",	T_INT32, (ffsize)trkbar_range },
	{ "onscroll",	T_STR, (ffsize)trkbar_onscroll },
	{ "value",	T_INT32, (ffsize)trkbar_val },
	{ NULL,	T_CLOSE, (ffsize)trkbar_done },
};

static int trkbar_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->trkbar = ldr_getctl(g, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;

	if (0 != ffui_trk_create(g->trkbar, g->wnd))
		return FFCONF_ESYS;
	ffconf_scheme_addctx(cs, trkbar_args, g);
	g->flags = 0;
	return 0;
}


// TAB
static int tab_onchange(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFCONF_EBADVAL;

	g->tab->change_id = id;
	return 0;
}
static const ffconf_arg tab_args[] = {
	{ "onchange",	T_STR, (ffsize)tab_onchange },
	{}
};
static int tab_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->tab = ldr_getctl(g, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;

	if (0 != ffui_tab_create(g->tab, g->wnd))
		return FFCONF_ESYS;
	ffconf_scheme_addctx(cs, tab_args, g);
	return 0;
}


// VIEW COL
static int viewcol_width(ffconf_scheme *cs, ffui_loader *g, ffint64 val)
{
	ffui_viewcol_setwidth(&g->vicol, val);
	return 0;
}

static int viewcol_done(ffconf_scheme *cs, ffui_loader *g)
{
	ffui_view_inscol(g->vi, -1, &g->vicol);
	return 0;
}

static const ffconf_arg viewcol_args[] = {
	{ "width",	T_INT32, (ffsize)viewcol_width },
	{ NULL,	T_CLOSE, (ffsize)viewcol_done },
};

static int viewcol_new(ffconf_scheme *cs, ffui_loader *g)
{
	ffstr *name = ffconf_scheme_objval(cs);
	ffui_viewcol_reset(&g->vicol);
	ffui_viewcol_setwidth(&g->vicol, 100);
	ffstr s = vars_val(&g->vars, *name);
	ffui_viewcol_settext(&g->vicol, s.ptr, s.len);
	ffconf_scheme_addctx(cs, viewcol_args, g);
	return 0;
}

// VIEW

enum {
	VIEW_STYLE_EDITABLE,
	VIEW_STYLE_GRID_LINES,
	VIEW_STYLE_MULTI_SELECT,
};
static const char *const view_styles_sorted[] = {
	"editable",
	"grid_lines",
	"multi_select",
};
static int view_style(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int n = ffszarr_ifindsorted(view_styles_sorted, FF_COUNT(view_styles_sorted), val->ptr, val->len);
	switch (n) {

	case VIEW_STYLE_GRID_LINES:
		ffui_view_style(g->vi, FFUI_VIEW_GRIDLINES, FFUI_VIEW_GRIDLINES);
		break;

	case VIEW_STYLE_MULTI_SELECT:
		ffui_view_style(g->vi, FFUI_VIEW_MULTI_SELECT, FFUI_VIEW_MULTI_SELECT);
		break;

	case VIEW_STYLE_EDITABLE:
		ffui_view_style(g->vi, FFUI_VIEW_EDITABLE, FFUI_VIEW_EDITABLE);
		break;

	default:
		return FFCONF_EBADVAL;
	}
	return 0;
}

static int view_dblclick(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	if (0 == (g->vi->dblclick_id = g->getcmd(g->udata, val)))
		return FFCONF_EBADVAL;
	return 0;
}

static const ffconf_arg view_args[] = {
	{ "style",	T_STRLIST, (ffsize)view_style },
	{ "dblclick",	T_STR, (ffsize)view_dblclick },
	{ "column",	T_OBJMULTI, (ffsize)viewcol_new },
	{}
};

static int view_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_view_create(g->vi, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, view_args, g);
	return 0;
}


// STATUSBAR
static const ffconf_arg stbar_args[] = {
	{}
};
static int stbar_new(ffconf_scheme *cs, ffui_loader *g)
{
	g->ctl = ldr_getctl(g, ffconf_scheme_objval(cs));
	if (g->ctl == NULL)
		return FFCONF_EBADVAL;

	if (0 != ffui_stbar_create(g->ctl, g->wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, stbar_args, g);
	return 0;
}


// TRAYICON
static int tray_lclick(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	int id = g->getcmd(g->udata, val);
	if (id == 0)
		return FFCONF_EBADVAL;
	g->trayicon->lclick_id = id;
	return 0;
}
static const ffconf_arg tray_args[] = {
	{ "lclick",	T_STR, (ffsize)tray_lclick },
	{}
};
static int tray_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->trayicon = ldr_getctl(g, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;

	ffui_tray_create(g->trayicon, g->wnd);
	ffconf_scheme_addctx(cs, tray_args, g);
	return 0;
}


// DIALOG
static const ffconf_arg dlg_args[] = {
	{}
};
static int dlg_new(ffconf_scheme *cs, ffui_loader *g)
{
	if (NULL == (g->dlg = g->getctl(g->udata, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;
	ffui_dlg_init(g->dlg);
	ffconf_scheme_addctx(cs, dlg_args, g);
	return 0;
}


// WINDOW
static int wnd_title(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	ffstr s = vars_val(&g->vars, *val);
	ffui_wnd_settextstr(g->wnd, &s);
	return 0;
}

static int wnd_popupfor(ffconf_scheme *cs, ffui_loader *g, const ffstr *val)
{
	void *parent;
	if (NULL == (parent = g->getctl(g->udata, val)))
		return FFCONF_EBADVAL;
	ffui_wnd_setpopup(g->wnd, parent);
	return 0;
}

static int wnd_position(ffconf_scheme *cs, ffui_loader *g, ffint64 v)
{
	int *i = &g->r.x;
	if (g->list_idx == 4)
		return FFCONF_EBADVAL;
	i[g->list_idx] = (int)v;
	if (g->list_idx == 3) {
		ffui_wnd_setplacement(g->wnd, 0, &g->r);
	}
	g->list_idx++;
	return 0;
}

static int wnd_icon(ffconf_scheme *cs, ffui_loader *g)
{
	ffmem_tzero(&g->ico);
	ffconf_scheme_addctx(cs, icon_args, &g->ico);
	return 0;
}

static int wnd_done(ffconf_scheme *cs, ffui_loader *g)
{
	if (g->ico.fn != NULL) {
		ffui_icon ico;
		char *sz = ffsz_allocfmt("%S/%s", &g->path, g->ico.fn);
		ffui_icon_load(&ico, sz);
		ffmem_free(sz);
		ffui_wnd_seticon(g->wnd, &ico);
		// ffmem_free(g->ico.fn);
		// ffmem_tzero(&g->ico);
	}

	if (g->accels.len != 0) {
		int r = ffui_wnd_hotkeys(g->wnd, (ffui_wnd_hotkey*)g->accels.ptr, g->accels.len);
		g->accels.len = 0;
		if (r != 0)
			return FFCONF_ESYS;
	}

	return 0;
}

static const ffconf_arg wnd_args[] = {
	{ "title",	T_STR, (ffsize)wnd_title },
	{ "position",	FFCONF_TINT32 | FFCONF_FSIGN | FFCONF_FLIST, (ffsize)wnd_position },
	{ "icon",	T_OBJ, (ffsize)wnd_icon },
	{ "popupfor",	T_STR, (ffsize)wnd_popupfor },

	{ "mainmenu",	T_OBJ, (ffsize)mmenu_new },
	{ "button",	T_OBJMULTI, (ffsize)btn_new },
	{ "checkbox",	T_OBJMULTI, (ffsize)chbox_new },
	{ "label",	T_OBJMULTI, (ffsize)lbl_new },
	{ "image",	T_OBJMULTI, (ffsize)img_new },
	{ "editbox",	T_OBJMULTI, (ffsize)edit_new },
	{ "text",	T_OBJMULTI, (ffsize)text_new },
	{ "trackbar",	T_OBJMULTI, (ffsize)trkbar_new },
	{ "tab",	T_OBJMULTI, (ffsize)tab_new },
	{ "listview",	T_OBJMULTI, (ffsize)view_new },
	{ "statusbar",	T_OBJ, (ffsize)stbar_new },
	{ "trayicon",	T_OBJ, (ffsize)tray_new },
	{ NULL,	T_CLOSE, (ffsize)wnd_done },
};

static int wnd_new(ffconf_scheme *cs, ffui_loader *g)
{
	ffui_wnd *wnd;

	if (NULL == (wnd = g->getctl(g->udata, ffconf_scheme_objval(cs))))
		return FFCONF_EBADVAL;
	ffmem_zero((byte*)g + FFOFF(ffui_loader, wnd), sizeof(ffui_loader) - FFOFF(ffui_loader, wnd));
	g->wnd = wnd;
	if (NULL == (g->wndname = ffsz_dupn(cs->objval.ptr, cs->objval.len)))
		return FFCONF_ESYS;
	g->ctl = (ffui_ctl*)wnd;
	if (0 != ffui_wnd_create(wnd))
		return FFCONF_ESYS;

	ffconf_scheme_addctx(cs, wnd_args, g);
	return 0;
}


// LANGUAGE
static int inc_lang_entry(ffconf_scheme *cs, ffui_loader *g, ffstr *fn)
{
	const ffstr *lang = ffconf_scheme_keyname(cs);
	if (!(ffstr_eqz(lang, "default")
		|| ffstr_eq(lang, g->language, 2)))
		return 0;
	if (g->lang_data.len != 0)
		return FFCONF_EBADVAL;

	if (ffstr_findanyz(fn, "/\\") >= 0)
		return FFCONF_EBADVAL;

	int rc = FFCONF_EBADVAL;
	char *ffn = ffsz_allocfmt("%S/%S", &g->path, fn);
	ffvec d = {};
	if (0 != fffile_readwhole(ffn, &d, 64*1024))
		goto end;

	rc = vars_load(&g->vars, (ffstr*)&d);

end:
	if (rc == 0) {
		if (ffstr_eqz(lang, "default"))
			g->lang_data_def = d;
		else
			g->lang_data = d;
	}
	ffmem_free(ffn);
	return rc;
}
static const ffconf_arg inc_lang_args[] = {
	{ "*", T_STRMULTI, (ffsize)inc_lang_entry },
	{}
};
static int inc_lang(ffconf_scheme *cs, ffui_loader *g)
{
	ffconf_scheme_addctx(cs, inc_lang_args, g);
	return 0;
}


static const ffconf_arg top_args[] = {
	{ "window",	T_OBJMULTI, (ffsize)wnd_new },
	{ "menu",	T_OBJMULTI, (ffsize)menu_new },
	{ "dialog",	T_OBJMULTI, (ffsize)dlg_new },
	{ "include_language",	T_OBJ, (ffsize)inc_lang },
	{}
};

void ffui_ldr_init2(ffui_loader *g, ffui_ldr_getctl_t getctl, ffui_ldr_getcmd_t getcmd, void *udata)
{
	ffmem_tzero(g);
	g->getctl = getctl;
	g->getcmd = getcmd;
	g->udata = udata;
	vars_init(&g->vars);
}

void ffui_ldr_fin(ffui_loader *g)
{
	ffvec_free(&g->lang_data_def);
	ffvec_free(&g->lang_data);
	ffmem_free(g->ico_ctl.fn);
	ffmem_free(g->ico.fn);
	ffvec_free(&g->accels);
	ffmem_free(g->errstr);
	ffmem_free0(g->wndname);
	vars_free(&g->vars);
}

int ffui_ldr_loadfile(ffui_loader *g, const char *fn)
{
	ffpath_splitpath(fn, ffsz_len(fn), &g->path, NULL);
	g->path.len += FFSLEN("/");

	ffstr errstr = {};
	int r = ffconf_parse_file(top_args, g, fn, 0, &errstr);
	if (r != 0) {
		ffmem_free(g->errstr);
		g->errstr = ffsz_dupstr(&errstr);
	}

	ffstr_free(&errstr);
	return r;
}


void ffui_ldrw_fin(ffui_loaderw *ldr)
{
	ffconfw_close(&ldr->confw);
}

void ffui_ldr_setv(ffui_loaderw *ldr, const char *const *names, size_t nn, uint flags)
{
	char buf[128];
	size_t n;
	uint i, f, set;
	ffui_ctl *c;
	ffstr settname, ctlname, val, s = {};

	for (i = 0;  i != nn;  i++) {
		ffstr_setz(&settname, names[i]);
		ffstr_rsplitby(&settname, '.', &ctlname, &val);

		if (NULL == (c = ldr->getctl(ldr->udata, &ctlname)))
			continue;

		set = 0;
		f = 0;
		switch (c->uid) {

		case FFUI_UID_WINDOW:
			if (ffstr_eqcz(&val, "position")) {
				ffui_pos pos;
				ffui_wnd_pos((ffui_wnd*)c, &pos);
				n = ffs_format_r0(buf, sizeof(buf), "%d %d %u %u"
					, pos.x, pos.y, pos.cx, pos.cy);
				ffstr_set(&s, buf, n);
				set = 1;
			}
			break;

		case FFUI_UID_TRACKBAR:
			if (ffstr_eqcz(&val, "value")) {
				ffconfw_addkeyz(&ldr->confw, settname.ptr);
				ffconfw_addint(&ldr->confw, ffui_trk_val(c));
			}
			break;

		default:
			continue;
		}

		if (!set)
			continue;

		ffui_ldr_set(ldr, settname.ptr, s.ptr, s.len, f);
	}
}

void ffui_ldr_set(ffui_loaderw *ldr, const char *name, const char *val, size_t len, uint flags)
{
	ffconfw_addkeyz(&ldr->confw, name);
	ffstr s;
	ffstr_set(&s, val, len);
	if (flags & FFUI_LDR_FSTR) {
		ffconfw_addstr(&ldr->confw, &s);
	} else
		ffconfw_add(&ldr->confw, FFCONF_TSTR | FFCONFW_FLINE | FFCONFW_FDONTESCAPE, &s);
}

int ffui_ldr_write(ffui_loaderw *ldr, const char *fn)
{
	ffstr buf;
	if (!ldr->fin) {
		ldr->fin = 1;
		ffconfw_fin(&ldr->confw);
	}
	ffconfw_output(&ldr->confw, &buf);
	return fffile_writewhole(fn, buf.ptr, buf.len, 0);
}

void ffui_ldr_loadconf(ffui_loader *g, const char *fn)
{
	ffvec buf = {};
	ffstr s, line, name, val;
	fffd f = FF_BADFD;

	if (FF_BADFD == (f = fffile_open(fn, O_RDONLY)))
		goto done;

	if (NULL == ffvec_alloc(&buf, fffile_size(f), 1))
		goto done;

	ffssize r = fffile_read(f, buf.ptr, buf.cap);
	if (r < 0)
		goto done;
	ffstr_set(&s, buf.ptr, r);

	g->f_loadconf = 1;
	while (s.len != 0) {
		ffstr_splitby(&s, '\n', &line, &s);

		if (line.len == 0)
			continue;

		ffstr_set(&name, line.ptr, 0);
		ffstr_null(&val);
		while (line.len != 0) {
			ffssize pos = ffstr_findany(&line, ". ", 2);
			if (pos < 0)
				break;
			if (line.ptr[pos] == ' ') {
				val = line;
				break;
			}
			name.len = line.ptr + pos - name.ptr;
			ffstr_shift(&line, pos + 1);
		}
		if (name.len == 0 || val.len == 0)
			continue;

		g->ctl = g->getctl(g->udata, &name);
		if (g->ctl != NULL) {
			g->list_idx = 0;
			ffconf conf = {};
			ffconf_init(&conf);
			ffconf_scheme cs = {};
			cs.parser = &conf;

			switch (g->ctl->uid) {
			case FFUI_UID_WINDOW:
				g->wnd = (void*)g->ctl;
				ffconf_scheme_addctx(&cs, wnd_args, g);
				break;

			case FFUI_UID_TRACKBAR:
				ffconf_scheme_addctx(&cs, trkbar_args, g);
				break;

			default:
				continue;
			}

			ffbool lf = 0;
			for (;;) {
				int r = ffconf_parse(&conf, &val);
				if (r < 0)
					goto done;
				else if (r == FFCONF_RMORE && !lf) {
					ffstr_setcz(&val, "\n");
					lf = 1;
					continue;
				}

				r = ffconf_scheme_process(&cs, r);
				if (r < 0)
					goto done;
				if (lf)
					break;
			}

			ffconf_scheme_destroy(&cs);
			ffconf_fin(&conf);
		}
	}

done:
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffvec_free(&buf);
	g->f_loadconf = 0;
}
