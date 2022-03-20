/** GUI based on GTK+.
Copyright (c) 2019 Simon Zolin
*/

#pragma once
#include <FFOS/types.h>
#include <FFOS/process.h>
#include "../string.h"
#include <gtk/gtk.h>


static inline void ffui_init()
{
	int argc = 0;
	char **argv = NULL;
	gtk_init(&argc, &argv);
}

#define ffui_uninit()


// ICON
// CONTROL
// MENU
// BUTTON
// CHECKBOX
// LABEL
// IMAGE
// EDITBOX
// TEXT
// TRACKBAR
// TAB
// LISTVIEW COLUMN
// LISTVIEW ITEM
// LISTVIEW
// STATUSBAR
// TRAYICON
// DIALOG
// WINDOW
// MESSAGE LOOP


typedef struct ffui_pos {
	int x, y
		, cx, cy;
} ffui_pos;


// ICON
typedef struct ffui_icon {
	GdkPixbuf *ico;
} ffui_icon;

static inline int ffui_icon_load(ffui_icon *ico, const char *filename)
{
	ico->ico = gdk_pixbuf_new_from_file(filename, NULL);
	return (ico->ico == NULL);
}

static inline int ffui_icon_loadimg(ffui_icon *ico, const char *filename, uint cx, uint cy, uint flags)
{
	ico->ico = gdk_pixbuf_new_from_file_at_scale(filename, cx, cy, 0, NULL);
	return (ico->ico == NULL);
}



// CONTROL

enum FFUI_UID {
	FFUI_UID_WINDOW = 1,
	FFUI_UID_TRACKBAR,
};

typedef struct ffui_wnd ffui_wnd;

#define _FFUI_CTL_MEMBERS \
	GtkWidget *h; \
	enum FFUI_UID uid; \
	ffui_wnd *wnd;

typedef struct ffui_ctl {
	_FFUI_CTL_MEMBERS
} ffui_ctl;

static inline void ffui_show(void *c, uint show)
{
	if (show)
		gtk_widget_show_all(((ffui_ctl*)c)->h);
	else
		gtk_widget_hide(((ffui_ctl*)c)->h);
}

#define ffui_ctl_destroy(c)  gtk_widget_destroy(((ffui_ctl*)c)->h)

#define ffui_setposrect(ctl, r) \
	gtk_widget_set_size_request((ctl)->h, (r)->cx, (r)->cy)


// MENU
typedef struct ffui_menu {
	_FFUI_CTL_MEMBERS
} ffui_menu;

static inline int ffui_menu_createmain(ffui_menu *m)
{
	m->h = gtk_menu_bar_new();
	return (m->h == NULL);
}

static inline int ffui_menu_create(ffui_menu *m)
{
	m->h = gtk_menu_new();
	return (m->h == NULL);
}

#define ffui_menu_new(text)  gtk_menu_item_new_with_mnemonic(text)
#define ffui_menu_newsep()  gtk_separator_menu_item_new()

FF_EXTERN void ffui_menu_setsubmenu(void *mi, ffui_menu *sub, ffui_wnd *wnd);

FF_EXTERN void ffui_menu_setcmd(void *mi, uint id);

static inline void ffui_menu_ins(ffui_menu *m, void *mi, int pos)
{
	gtk_menu_shell_insert(GTK_MENU_SHELL(m->h), GTK_WIDGET(mi), pos);
}


// BUTTON
typedef struct ffui_btn {
	_FFUI_CTL_MEMBERS
	uint action_id;
} ffui_btn;

FF_EXTERN int ffui_btn_create(ffui_btn *b, ffui_wnd *parent);
static inline void ffui_btn_settextz(ffui_btn *b, const char *textz)
{
	gtk_button_set_label(GTK_BUTTON(b->h), textz);
}
static inline void ffui_btn_settextstr(ffui_btn *b, const ffstr *text)
{
	char *sz = ffsz_dupstr(text);
	gtk_button_set_label(GTK_BUTTON(b->h), sz);
	ffmem_free(sz);
}

static inline void ffui_btn_seticon(ffui_btn *b, ffui_icon *ico)
{
	GtkWidget *img = gtk_image_new();
	gtk_image_set_from_pixbuf(GTK_IMAGE(img), ico->ico);
	gtk_button_set_image(GTK_BUTTON(b->h), img);
}


// CHECKBOX
typedef struct ffui_checkbox {
	_FFUI_CTL_MEMBERS
	uint action_id;
} ffui_checkbox;

void _ffui_checkbox_clicked(GtkWidget *widget, gpointer udata);
static inline int ffui_checkbox_create(ffui_checkbox *cb, ffui_wnd *parent)
{
	cb->h = gtk_check_button_new();
	cb->wnd = parent;
	g_signal_connect(cb->h, "clicked", G_CALLBACK(_ffui_checkbox_clicked), cb);
	return 0;
}
static inline void ffui_checkbox_settextz(ffui_checkbox *cb, const char *textz)
{
	gtk_button_set_label(GTK_BUTTON(cb->h), textz);
}
static inline void ffui_checkbox_settextstr(ffui_checkbox *cb, const ffstr *text)
{
	char *sz = ffsz_dupstr(text);
	gtk_button_set_label(GTK_BUTTON(cb->h), sz);
	ffmem_free(sz);
}
static inline ffbool ffui_checkbox_checked(ffui_checkbox *cb)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cb->h));
}
static inline void ffui_checkbox_check(ffui_checkbox *cb, int val)
{
	g_signal_handlers_block_by_func(cb->h, G_CALLBACK(_ffui_checkbox_clicked), cb);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb->h), val);
	g_signal_handlers_unblock_by_func(cb->h, G_CALLBACK(_ffui_checkbox_clicked), cb);
}


// LABEL
typedef struct ffui_label {
	_FFUI_CTL_MEMBERS
} ffui_label;

FF_EXTERN int ffui_lbl_create(ffui_label *l, ffui_wnd *parent);

#define ffui_lbl_settextz(l, text)  gtk_label_set_text(GTK_LABEL((l)->h), text)
static inline void ffui_lbl_settext(ffui_label *l, const char *text, size_t len)
{
	char *sz = ffsz_dupn(text, len);
	ffui_lbl_settextz(l, sz);
	ffmem_free(sz);
}
#define ffui_lbl_settextstr(l, str)  ffui_lbl_settext(l, (str)->ptr, (str)->len)


// IMAGE
typedef struct ffui_image {
	_FFUI_CTL_MEMBERS
} ffui_image;

static inline int ffui_image_create(ffui_image *c, ffui_wnd *parent)
{
	c->h = gtk_image_new();
	c->wnd = parent;
	return 0;
}

static inline void ffui_image_setfile(ffui_image *c, const char *fn)
{
	gtk_image_set_from_file(GTK_IMAGE(c->h), fn);
}

static inline void ffui_image_seticon(ffui_image *c, ffui_icon *ico)
{
	gtk_image_set_from_pixbuf(GTK_IMAGE(c->h), ico->ico);
}


// EDITBOX
typedef struct ffui_edit {
	_FFUI_CTL_MEMBERS
	uint change_id;
} ffui_edit;

FF_EXTERN int ffui_edit_create(ffui_edit *e, ffui_wnd *parent);

#define ffui_edit_settextz(e, text)  gtk_entry_set_text(GTK_ENTRY((e)->h), text)
static inline void ffui_edit_settext(ffui_edit *e, const char *text, size_t len)
{
	char *sz = ffsz_dupn(text, len);
	ffui_edit_settextz(e, sz);
	ffmem_free(sz);
}
#define ffui_edit_settextstr(e, str)  ffui_edit_settext(e, (str)->ptr, (str)->len)

static inline void ffui_edit_textstr(ffui_edit *e, ffstr *s)
{
	const gchar *sz = gtk_entry_get_text(GTK_ENTRY(e->h));
	ffsize len = ffsz_len(sz);
	char *p = ffsz_dupn(sz, len);
	FF_ASSERT(s->ptr == NULL);
	ffstr_set(s, p, len);
}

#define ffui_edit_sel(e, start, end) \
	gtk_editable_select_region(GTK_EDITABLE((e)->h), start, end);


// TEXT
typedef struct ffui_text {
	_FFUI_CTL_MEMBERS
} ffui_text;

static inline int ffui_text_create(ffui_text *t, ffui_wnd *parent)
{
	t->h = gtk_text_view_new();
	t->wnd = parent;
	return 0;
}

static inline void ffui_text_addtext(ffui_text *t, const char *text, size_t len)
{
	GtkTextBuffer *gbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->h));
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(gbuf, &end);
	gtk_text_buffer_insert(gbuf, &end, text, len);
}
#define ffui_text_addtextz(t, text)  ffui_text_addtext(t, text, ffsz_len(text))
#define ffui_text_addtextstr(t, str)  ffui_text_addtext(t, (str)->ptr, (str)->len)

static inline void ffui_text_clear(ffui_text *t)
{
	GtkTextBuffer *gbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->h));
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(gbuf, &start);
	gtk_text_buffer_get_end_iter(gbuf, &end);
	gtk_text_buffer_delete(gbuf, &start, &end);
}

static inline void ffui_text_scroll(ffui_text *t, int pos)
{
	GtkTextBuffer *gbuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->h));
	GtkTextIter iter;
	if (pos == -1)
		gtk_text_buffer_get_end_iter(gbuf, &iter);
	else
		return;
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(t->h), &iter, 0.0, 0, 0.0, 1.0);
}

/**
mode: GTK_WRAP_NONE GTK_WRAP_CHAR GTK_WRAP_WORD GTK_WRAP_WORD_CHAR*/
#define ffui_text_setwrapmode(t, mode)  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW((t)->h), mode)

#define ffui_text_setmonospace(t, mono)  gtk_text_view_set_monospace(GTK_TEXT_VIEW((t)->h), mono)


// TRACKBAR
typedef struct ffui_trkbar {
	_FFUI_CTL_MEMBERS
	uint scroll_id;
} ffui_trkbar;

FF_EXTERN int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent);

FF_EXTERN void ffui_trk_setrange(ffui_trkbar *t, uint max);

FF_EXTERN void ffui_trk_set(ffui_trkbar *t, uint val);

#define ffui_trk_val(t)  gtk_range_get_value(GTK_RANGE((t)->h))


// TAB
typedef struct ffui_tab {
	_FFUI_CTL_MEMBERS
	uint change_id;
	uint changed_index;
} ffui_tab;

FF_EXTERN int ffui_tab_create(ffui_tab *t, ffui_wnd *parent);

FF_EXTERN void ffui_tab_ins(ffui_tab *t, int idx, const char *textz);

#define ffui_tab_append(t, textz)  ffui_tab_ins(t, -1, textz)

#define ffui_tab_del(t, idx)  gtk_notebook_remove_page(GTK_NOTEBOOK((t)->h), idx)

#define ffui_tab_count(t)  gtk_notebook_get_n_pages(GTK_NOTEBOOK((t)->h))

#define ffui_tab_active(t)  gtk_notebook_get_current_page(GTK_NOTEBOOK((t)->h))

FF_EXTERN void ffui_tab_setactive(ffui_tab *t, int idx);


// LISTVIEW COLUMN
typedef struct ffui_view ffui_view;
typedef struct ffui_viewcol {
	char *text;
	uint width;
} ffui_viewcol;

#define ffui_viewcol_reset(vc)  ffmem_free0((vc)->text)

static inline void ffui_viewcol_settext(ffui_viewcol *vc, const char *text, size_t len)
{
	vc->text = ffsz_dupn(text, len);
}

#define ffui_viewcol_setwidth(vc, w)  (vc)->width = (w)
#define ffui_viewcol_width(vc)  ((vc)->width)

FF_EXTERN void ffui_view_inscol(ffui_view *v, int pos, ffui_viewcol *vc);
FF_EXTERN void ffui_view_setcol(ffui_view *v, int pos, ffui_viewcol *vc);

/** Set column width */
static inline void ffui_view_setcol_width(ffui_view *v, int pos, uint width)
{
	ffui_viewcol vc = { .width = width };
	ffui_view_setcol(v, pos, &vc);
}

FF_EXTERN void ffui_view_col(ffui_view *v, int pos, ffui_viewcol *vc);

/** Get column width */
static inline uint ffui_view_col_width(ffui_view *v, int pos)
{
	ffui_viewcol vc = {};
	ffui_view_col(v, pos, &vc);
	return vc.width;
}

/** Get the number of columns. */
#define ffui_view_ncols(v) \
	gtk_tree_model_get_n_columns(GTK_TREE_MODEL((v)->store))


// LISTVIEW ITEM
typedef struct ffui_viewitem {
	char *text;
	int idx;
	uint text_alloc :1;
} ffui_viewitem;

static inline void ffui_view_iteminit(ffui_viewitem *it)
{
	ffmem_tzero(it);
}

static inline void ffui_view_itemreset(ffui_viewitem *it)
{
	if (it->text_alloc) {
		ffmem_free0(it->text);
		it->text_alloc = 0;
	}
}

#define ffui_view_setindex(it, i)  (it)->idx = (i)

#define ffui_view_settextz(it, sz)  (it)->text = (char*)(sz)
static inline void ffui_view_settext(ffui_viewitem *it, const char *text, size_t len)
{
	it->text = ffsz_dupn(text, len);
	it->text_alloc = 1;
}
#define ffui_view_settextstr(it, str)  ffui_view_settext(it, (str)->ptr, (str)->len)


// LISTVIEW
struct ffui_view_disp {
	uint idx;
	uint sub;
	ffstr text;
};

struct ffui_view {
	_FFUI_CTL_MEMBERS
	GtkTreeModel *store;
	GtkCellRenderer *rend;
	uint dblclick_id;
	uint dropfile_id;
	uint dispinfo_id;
	uint edit_id;
	ffui_menu *popup_menu;

	union {
	GtkTreePath *path; // dblclick_id
	struct { // edit_id
		ffuint idx;
		const char *new_text;
	} edited;
	ffstr drop_data;
	struct ffui_view_disp disp;
	};
};

FF_EXTERN int ffui_view_create(ffui_view *v, ffui_wnd *parent);

enum FFUI_VIEW_STYLE {
	FFUI_VIEW_GRIDLINES = 1,
	FFUI_VIEW_MULTI_SELECT = 2,
	FFUI_VIEW_EDITABLE = 4,
};
FF_EXTERN void ffui_view_style(ffui_view *v, uint flags, uint set);

#define ffui_view_nitems(v)  gtk_tree_model_iter_n_children((void*)(v)->store, NULL)

/**
first: first index to add or redraw
delta: 0:redraw row */
FF_EXTERN void ffui_view_setdata(ffui_view *v, uint first, int delta);

static inline void ffui_view_clear(ffui_view *v)
{
	if (v->store != NULL)
		gtk_list_store_clear(GTK_LIST_STORE(v->store));
}

#define ffui_view_selall(v)  gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW((v)->h)))
#define ffui_view_unselall(v)  gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW((v)->h)))

typedef struct ffui_sel {
	ffsize len;
	char *ptr;
	ffsize off;
} ffui_sel;

/** Get array of selected items.
Return ffui_sel*.  Free with ffui_view_sel_free(). */
FF_EXTERN ffui_sel* ffui_view_getsel(ffui_view *v);

static inline void ffui_view_sel_free(ffui_sel *sel)
{
	if (sel == NULL)
		return;
	ffmem_free(sel->ptr);
	ffmem_free(sel);
}

/** Get next selected item. */
static inline int ffui_view_selnext(ffui_view *v, ffui_sel *sel)
{
	if (sel->off == sel->len)
		return -1;
	return *ffslice_itemT(sel, sel->off++, uint);
}

FF_EXTERN void ffui_view_dragdrop(ffui_view *v, uint action_id);

/** Get next drag'n'drop file name.
Return -1 if no more. */
FF_EXTERN int ffui_fdrop_next(ffvec *fn, ffstr *dropdata);


/**
Note: must be called only from wnd.on_action(). */
static inline int ffui_view_focused(ffui_view *v)
{
	FF_ASSERT(v->path != NULL);
	int *i = gtk_tree_path_get_indices(v->path);
	return i[0];
}

FF_EXTERN void ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it);

#define ffui_view_append(v, it)  ffui_view_ins(v, -1, it)

FF_EXTERN void ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it);

FF_EXTERN void ffui_view_rm(ffui_view *v, ffui_viewitem *it);

static inline void ffui_view_scroll_idx(ffui_view *v, uint idx)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices(idx, -1);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(v->h), path, NULL, 0, 0, 0);
	gtk_tree_path_free(path);
}

static inline uint ffui_view_scroll_vert(ffui_view *v)
{
	GtkAdjustment *a = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->h));
	double d = gtk_adjustment_get_value(a);
	return d * 100;
}

static inline void ffui_view_scroll_setvert(ffui_view *v, uint val)
{
	GtkAdjustment *a = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->h));
	gtk_adjustment_set_value(a, (double)val / 100);
}

static inline void ffui_view_popupmenu(ffui_view *v, ffui_menu *m)
{
	v->popup_menu = m;
	if (m != NULL) {
		g_object_set_data(G_OBJECT(m->h), "ffdata", v->wnd);
		gtk_widget_show_all(m->h);
	}
}


// STATUSBAR

FF_EXTERN int ffui_stbar_create(ffui_ctl *sb, ffui_wnd *parent);

static inline void ffui_stbar_settextz(ffui_ctl *sb, const char *text)
{
	gtk_statusbar_push(GTK_STATUSBAR(sb->h), gtk_statusbar_get_context_id(GTK_STATUSBAR(sb->h), "a"), text);
}


// TRAYICON
typedef struct ffui_trayicon {
	_FFUI_CTL_MEMBERS
	uint lclick_id;
} ffui_trayicon;

FF_EXTERN int ffui_tray_create(ffui_trayicon *t, ffui_wnd *wnd);

#define ffui_tray_hasicon(t) \
	(0 != gtk_status_icon_get_size(GTK_STATUS_ICON((t)->h)))

static inline void ffui_tray_seticon(ffui_trayicon *t, ffui_icon *ico)
{
	gtk_status_icon_set_from_pixbuf(GTK_STATUS_ICON(t->h), ico->ico);
}

#define ffui_tray_show(t, show)  gtk_status_icon_set_visible(GTK_STATUS_ICON((t)->h), show)


// DIALOG

typedef struct ffui_dialog {
	char *title;
	char *name;
	GSList *names;
	GSList *curname;
	uint multisel :1;
} ffui_dialog;

static inline void ffui_dlg_init(ffui_dialog *d)
{
}

static inline void ffui_dlg_destroy(ffui_dialog *d)
{
	g_slist_free_full(d->names, g_free);  d->names = NULL;
	ffmem_free0(d->title);
	g_free(d->name); d->name = NULL;
}

static inline void ffui_dlg_titlez(ffui_dialog *d, const char *sz)
{
	d->title = ffsz_dup(sz);
}

#define ffui_dlg_multisel(d, val)  ((d)->multisel = (val))

/** Get the next file name (for a dialog with multiselect). */
FF_EXTERN char* ffui_dlg_nextname(ffui_dialog *d);

FF_EXTERN char* ffui_dlg_open(ffui_dialog *d, ffui_wnd *parent);

FF_EXTERN char* ffui_dlg_save(ffui_dialog *d, ffui_wnd *parent, const char *fn, size_t fnlen);


// WINDOW
struct ffui_wnd {
	GtkWindow *h;
	enum FFUI_UID uid;
	GtkWidget *vbox;

	void (*on_create)(ffui_wnd *wnd);
	void (*on_destroy)(ffui_wnd *wnd);
	void (*on_action)(ffui_wnd *wnd, int id);

	uint onclose_id;
	uint hide_on_close :1;
};

static inline int ffui_wnd_initstyle()
{
	return 0;
}

FF_EXTERN int ffui_wnd_create(ffui_wnd *w);

#define ffui_wnd_close(w)  gtk_window_close((w)->h)

#define ffui_wnd_destroy(w)  gtk_widget_destroy(GTK_WIDGET((w)->h))

FF_EXTERN void ffui_wnd_setpopup(ffui_wnd *w, ffui_wnd *parent);

#define ffui_wnd_present(w)  gtk_window_present((w)->h)

static inline void ffui_wnd_setmenu(ffui_wnd *w, ffui_menu *m)
{
	m->wnd = w;
	gtk_box_pack_start(GTK_BOX(w->vbox), m->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
}

typedef uint ffui_hotkey;

#define ffui_hotkey_mod(hk)  ((hk) >> 16)
#define ffui_hotkey_key(hk)  ((hk) & 0xffff)

/** Parse hotkey string, e.g. "Ctrl+Alt+Shift+Q".
Return: low-word: char key or vkey, hi-word: control flags;  0 on error. */
FF_EXTERN ffui_hotkey ffui_hotkey_parse(const char *s, size_t len);

typedef struct ffui_wnd_hotkey {
	ffui_hotkey hk;
	GtkWidget *h;
} ffui_wnd_hotkey;

/** Set hotkey table. */
FF_EXTERN int ffui_wnd_hotkeys(ffui_wnd *w, const ffui_wnd_hotkey *hotkeys, size_t n);

#define ffui_wnd_settextz(w, text)  gtk_window_set_title((w)->h, text)
static inline void ffui_wnd_settextstr(ffui_wnd *w, const ffstr *str)
{
	char *sz = ffsz_dupstr(str);
	ffui_wnd_settextz(w, sz);
	ffmem_free(sz);
}

#define ffui_wnd_seticon(w, icon)  gtk_window_set_icon((w)->h, (icon)->ico)

static inline void ffui_wnd_pos(ffui_wnd *w, ffui_pos *pos)
{
	int x, y, ww, h;
	gtk_window_get_position(w->h, &x, &y);
	gtk_window_get_size(w->h, &ww, &h);
	pos->x = x;
	pos->y = y;
	pos->cx = ww;
	pos->cy = h;
}

static inline void ffui_wnd_setplacement(ffui_wnd *w, uint showcmd, const ffui_pos *pos)
{
	gtk_window_move(w->h, pos->x, pos->y);
	gtk_window_set_default_size(w->h, pos->cx, pos->cy);
}


// MESSAGE LOOP
FF_EXTERN void ffui_run();

#define ffui_quitloop()  gtk_main_quit()

typedef void (*ffui_handler)(void *param);

enum {
	FFUI_POST_WAIT = 1 << 31,
};

/**
flags: FFUI_POST_WAIT */
FF_EXTERN void ffui_thd_post(ffui_handler func, void *udata, uint flags);

enum FFUI_MSG {
	FFUI_QUITLOOP,
	FFUI_CHECKBOX_SETTEXTZ,
	FFUI_CLIP_SETTEXT,
	FFUI_EDIT_GETTEXT,
	FFUI_LBL_SETTEXT,
	FFUI_STBAR_SETTEXT,
	FFUI_TAB_ACTIVE,
	FFUI_TAB_COUNT,
	FFUI_TAB_INS,
	FFUI_TAB_SETACTIVE,
	FFUI_TEXT_ADDTEXT,
	FFUI_TEXT_SETTEXT,
	FFUI_TRK_SET,
	FFUI_TRK_SETRANGE,
	FFUI_VIEW_CLEAR,
	FFUI_VIEW_GETSEL,
	FFUI_VIEW_RM,
	FFUI_VIEW_SCROLLSET,
	FFUI_VIEW_SETDATA,
	FFUI_WND_SETTEXT,
	FFUI_WND_SHOW,
};

/**
id: enum FFUI_MSG */
FF_EXTERN void ffui_post(void *ctl, uint id, void *udata);
FF_EXTERN size_t ffui_send(void *ctl, uint id, void *udata);

#define ffui_post_quitloop()  ffui_post(NULL, FFUI_QUITLOOP, NULL)
#define ffui_send_lbl_settext(ctl, sz)  ffui_send(ctl, FFUI_LBL_SETTEXT, (void*)sz)
#define ffui_send_edit_textstr(ctl, str_dst)  ffui_send(ctl, FFUI_EDIT_GETTEXT, str_dst)
#define ffui_send_text_settextstr(ctl, str)  ffui_send(ctl, FFUI_TEXT_SETTEXT, (void*)str)
#define ffui_send_text_addtextstr(ctl, str)  ffui_send(ctl, FFUI_TEXT_ADDTEXT, (void*)str)
#define ffui_send_checkbox_settextz(ctl, sz)  ffui_send(ctl, FFUI_CHECKBOX_SETTEXTZ, (void*)sz)
#define ffui_send_wnd_settext(ctl, sz)  ffui_send(ctl, FFUI_WND_SETTEXT, (void*)sz)
#define ffui_post_wnd_show(ctl, show)  ffui_send(ctl, FFUI_WND_SHOW, (void*)(ffsize)show)
#define ffui_send_view_rm(ctl, it)  ffui_send(ctl, FFUI_VIEW_RM, it)
#define ffui_post_view_clear(ctl)  ffui_post(ctl, FFUI_VIEW_CLEAR, NULL)
#define ffui_post_view_scroll_set(ctl, vert_pos)  ffui_post(ctl, FFUI_VIEW_SCROLLSET, (void*)(size_t)vert_pos)
#define ffui_clipboard_settextstr(str)  ffui_send(NULL, FFUI_CLIP_SETTEXT, (void*)str)

/** See ffui_view_getsel().
Return ffui_sel* */
#define ffui_send_view_getsel(v)  ffui_send(v, FFUI_VIEW_GETSEL, NULL)
static inline void ffui_send_view_setdata(ffui_view *v, uint first, int delta)
{
	size_t p = ((first & 0xffff) << 16) | (delta & 0xffff);
	ffui_send(v, FFUI_VIEW_SETDATA, (void*)p);
}
static inline void ffui_post_view_setdata(ffui_view *v, uint first, int delta)
{
	size_t p = ((first & 0xffff) << 16) | (delta & 0xffff);
	ffui_post(v, FFUI_VIEW_SETDATA, (void*)p);
}
#define ffui_post_trk_setrange(ctl, range)  ffui_post(ctl, FFUI_TRK_SETRANGE, (void*)(size_t)range)
#define ffui_post_trk_set(ctl, val)  ffui_post(ctl, FFUI_TRK_SET, (void*)(size_t)val)

#define ffui_send_tab_ins(ctl, textz)  ffui_send(ctl, FFUI_TAB_INS, textz)
#define ffui_send_tab_setactive(ctl, idx)  ffui_send(ctl, FFUI_TAB_SETACTIVE, (void*)(ffsize)idx)
static inline int ffui_send_tab_active(ffui_tab *ctl)
{
	ffsize idx;
	ffui_send(ctl, FFUI_TAB_ACTIVE, &idx);
	return idx;
}
static inline int ffui_send_tab_count(ffui_tab *ctl)
{
	ffsize n;
	ffui_send(ctl, FFUI_TAB_COUNT, &n);
	return n;
}

#define ffui_send_stbar_settextz(sb, sz)  ffui_send(sb, FFUI_STBAR_SETTEXT, (void*)sz)


/** Exec and wait
Return exit code or -1 on error */
static inline int _ffui_ps_exec_wait(const char *filename, const char **argv, const char **env)
{
	ffps_execinfo info = {};
	info.argv = argv;
	info.env = env;
	ffps ps = ffps_exec_info(filename, &info);
	if (ps == FFPS_NULL)
		return -1;

	int code;
	if (0 != ffps_wait(ps, -1, &code))
		return -1;

	return code;
}

/** Move files to Trash */
static inline int ffui_glib_trash(const char **names, ffsize n)
{
	ffvec v = {};
	if (NULL == ffvec_allocT(&v, 3 + n, char*))
		return -1;
	char **p = (char**)v.ptr;
	*p++ = "/usr/bin/gio";
	*p++ = "trash";
	for (ffsize i = 0;  i != n;  i++) {
		*p++ = (char*)names[i];
	}
	*p++ = NULL;
	int r = _ffui_ps_exec_wait(((char**)v.ptr)[0], (const char**)v.ptr, (const char**)environ);
	ffvec_free(&v);
	return r;
}
