/**
Copyright (c) 2019 Simon Zolin
*/

#include "gtk.h"
#include "../http1.h"
#include <FFOS/atomic.h>
#include <FFOS/thread.h>


// MENU
// BUTTON
// CHECKBOX
// LABEL
// EDITBOX
// TRACKBAR
// TAB
// LISTVIEW
// STATUSBAR
// TRAYICON
// DIALOG
// WINDOW
// MESSAGE LOOP

ffuint64 _ffui_thd_id;

void ffui_run()
{
	_ffui_thd_id = ffthread_curid();
	gtk_main();
}

#define sig_disable(h, func, udata) \
	g_signal_handlers_disconnect_matched(h, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, 0, G_CALLBACK(func), udata)


// MENU
static void _ffui_menu_activate(GtkWidget *mi, gpointer udata)
{
	GtkWidget *parent_menu = mi;
	ffui_wnd *wnd = NULL;
	while (wnd == NULL) {
		parent_menu = gtk_widget_get_parent(parent_menu);
		wnd = g_object_get_data(G_OBJECT(parent_menu), "ffdata");
	}
	uint id = (size_t)udata;
	wnd->on_action(wnd, id);
}

void ffui_menu_setcmd(void *mi, uint id)
{
	g_signal_connect(mi, "activate", G_CALLBACK(&_ffui_menu_activate), (void*)(size_t)id);
}

void ffui_menu_setsubmenu(void *mi, ffui_menu *sub, ffui_wnd *wnd)
{
	gtk_menu_item_set_submenu(mi, sub->h);
	g_object_set_data(G_OBJECT(sub->h), "ffdata", wnd);
}


// BUTTON
static void _ffui_btn_clicked(GtkWidget *widget, gpointer udata)
{
	ffui_btn *b = udata;
	b->wnd->on_action(b->wnd, b->action_id);
}

int ffui_btn_create(ffui_btn *b, ffui_wnd *parent)
{
	b->h = gtk_button_new();
	b->wnd = parent;
	g_signal_connect(b->h, "clicked", G_CALLBACK(&_ffui_btn_clicked), b);
	return 0;
}


// CHECKBOX
void _ffui_checkbox_clicked(GtkWidget *widget, gpointer udata)
{
	ffui_checkbox *cb = udata;
	cb->wnd->on_action(cb->wnd, cb->action_id);
}


// LABEL
int ffui_lbl_create(ffui_label *l, ffui_wnd *parent)
{
	l->h = gtk_label_new("");
	l->wnd = parent;
	return 0;
}


// EDITBOX
static void _ffui_edit_changed(GtkEditable *editable, gpointer udata)
{
	ffui_edit *e = udata;
	if (e->change_id != 0)
		e->wnd->on_action(e->wnd, e->change_id);
}

int ffui_edit_create(ffui_edit *e, ffui_wnd *parent)
{
	e->h = gtk_entry_new();
	e->wnd = parent;
	g_signal_connect(e->h, "changed", G_CALLBACK(_ffui_edit_changed), e);
	return 0;
}


// TRACKBAR
static void _ffui_trk_value_changed(GtkWidget *widget, gpointer udata)
{
	ffui_trkbar *t = udata;
	if (t->scroll_id != 0)
		t->wnd->on_action(t->wnd, t->scroll_id);
}

int ffui_trk_create(ffui_trkbar *t, ffui_wnd *parent)
{
	t->uid = FFUI_UID_TRACKBAR;
	t->h = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(t->h), 0);
	t->wnd = parent;
	g_signal_connect(t->h, "value-changed", G_CALLBACK(&_ffui_trk_value_changed), t);
	return 0;
}

void ffui_trk_setrange(ffui_trkbar *t, uint max)
{
	g_signal_handlers_block_by_func(t->h, G_CALLBACK(&_ffui_trk_value_changed), t);
	gtk_range_set_range(GTK_RANGE((t)->h), 0, max);
	g_signal_handlers_unblock_by_func(t->h, G_CALLBACK(&_ffui_trk_value_changed), t);
}

void ffui_trk_set(ffui_trkbar *t, uint val)
{
	g_signal_handlers_block_by_func(t->h, G_CALLBACK(&_ffui_trk_value_changed), t);
	gtk_range_set_value(GTK_RANGE(t->h), val);
	g_signal_handlers_unblock_by_func(t->h, G_CALLBACK(&_ffui_trk_value_changed), t);
}


// TAB
static void _ffui_tab_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer udata)
{
	ffui_tab *t = udata;
	t->changed_index = page_num;
	t->wnd->on_action(t->wnd, t->change_id);
}

int ffui_tab_create(ffui_tab *t, ffui_wnd *parent)
{
	if (NULL == (t->h = gtk_notebook_new()))
		return -1;
	t->wnd = parent;
	gtk_box_pack_start(GTK_BOX(parent->vbox), t->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	g_signal_connect(t->h, "switch-page", G_CALLBACK(&_ffui_tab_switch_page), t);
	return 0;
}

void ffui_tab_ins(ffui_tab *t, int idx, const char *textz)
{
	GtkWidget *label = gtk_label_new(textz);
	GtkWidget *child = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_signal_handlers_block_by_func(t->h, G_CALLBACK(&_ffui_tab_switch_page), t);
	gtk_notebook_insert_page(GTK_NOTEBOOK(t->h), child, label, idx);
	gtk_widget_show_all(t->h);
	g_signal_handlers_unblock_by_func(t->h, G_CALLBACK(&_ffui_tab_switch_page), t);
}

void ffui_tab_setactive(ffui_tab *t, int idx)
{
	g_signal_handlers_block_by_func(t->h, G_CALLBACK(&_ffui_tab_switch_page), t);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(t->h), idx);
	g_signal_handlers_unblock_by_func(t->h, G_CALLBACK(&_ffui_tab_switch_page), t);
}


// LISTVIEW
static void _ffui_view_row_activated(void *a, GtkTreePath *path, void *c, gpointer udata)
{
	FFDBG_PRINTLN(10, "udata: %p", udata);
	ffui_view *v = udata;
	v->path = path;
	v->wnd->on_action(v->wnd, v->dblclick_id);
	v->path = NULL;
}

static void _ffui_view_drag_data_received(GtkWidget *wgt, GdkDragContext *context, int x, int y,
	GtkSelectionData *seldata, guint info, guint time, gpointer userdata)
{
	gint len;
	const void *ptr = gtk_selection_data_get_data_with_length(seldata, &len);
	FFDBG_PRINTLN(10, "seldata:[%u] %*s", len, (size_t)len, ptr);

	ffui_view *v = userdata;
	ffstr_set(&v->drop_data, ptr, len);
	v->wnd->on_action(v->wnd, v->dropfile_id);
	ffstr_null(&v->drop_data);
}

int ffui_fdrop_next(ffvec *fn, ffstr *dropdata)
{
	ffstr ln;
	while (dropdata->len != 0) {
		ffstr_splitby(dropdata, '\n', &ln, dropdata);
		ffstr_rskipchar1(&ln, '\r');
		if (!ffstr_matchz(&ln, "file://"))
			continue;
		ffstr_shift(&ln, FFSLEN("file://"));

		if (NULL == ffvec_realloc(fn, ln.len, 1))
			return -1;
		int r = httpurl_unescape(fn->ptr, fn->cap, ln);
		if (r < 0)
			return -1;
		fn->len = r;
		if (fn->len == 0)
			return -1;
		return 0;
	}
	return -1;
}

static void _ffui_view_cell_edited(GtkCellRendererText *cell, gchar *path_string, gchar *text, gpointer udata)
{
	ffui_view *v = udata;
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	int *ii = gtk_tree_path_get_indices(path);
	v->edited.idx = ii[0];
	v->edited.new_text = text;
	v->wnd->on_action(v->wnd, v->edit_id);
	v->edited.idx = 0;
	v->edited.new_text = NULL;
}

static gboolean _ffui_view_button_press_event(GtkWidget *w, GdkEventButton *ev, gpointer udata)
{
	if (ev->type == GDK_BUTTON_PRESS && ev->button == 3 /*right mouse button*/) {
		ffui_view *v = udata;
		if (v->popup_menu != NULL) {

			GtkTreePath *path;
			if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(v->h), ev->x, ev->y, &path, NULL, NULL, NULL))
				return 0;
			void *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(v->h));
			gtk_tree_selection_select_path(sel, path);
			gtk_tree_path_free(path);

			guint32 t = gdk_event_get_time((GdkEvent*)ev);
			gtk_menu_popup(GTK_MENU(v->popup_menu->h), NULL, NULL, NULL, NULL, ev->button, t);
			return 1;
		}
	}
	return 0;
}

int ffui_view_create(ffui_view *v, ffui_wnd *parent)
{
	v->h = gtk_tree_view_new();
	g_signal_connect(v->h, "row-activated", G_CALLBACK(&_ffui_view_row_activated), v);
	v->wnd = parent;
	v->rend = gtk_cell_renderer_text_new();
	g_signal_connect(v->rend, "edited", (GCallback)_ffui_view_cell_edited, v);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scroll), v->h);
	gtk_box_pack_start(GTK_BOX(parent->vbox), scroll, /*expand=*/1, /*fill=*/1, /*padding=*/0);
	g_signal_connect(v->h, "button-press-event", (GCallback)_ffui_view_button_press_event, v);
	return 0;
}

void ffui_view_style(ffui_view *v, uint flags, uint set)
{
	uint val;

	if (flags & FFUI_VIEW_GRIDLINES) {
		val = (set & FFUI_VIEW_GRIDLINES)
			? GTK_TREE_VIEW_GRID_LINES_BOTH
			: GTK_TREE_VIEW_GRID_LINES_NONE;
		gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(v->h), val);
	}

	if (flags & FFUI_VIEW_MULTI_SELECT) {
		val = (set & FFUI_VIEW_MULTI_SELECT)
			? GTK_SELECTION_MULTIPLE
			: GTK_SELECTION_SINGLE;
		gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(v->h)), val);
	}

	if (flags & FFUI_VIEW_EDITABLE) {
		val = !!(set & FFUI_VIEW_EDITABLE);
		g_object_set(v->rend, "editable", val, NULL);
	}
}

void ffui_view_inscol(ffui_view *v, int pos, ffui_viewcol *vc)
{
	FFDBG_PRINTLN(10, "pos:%d", pos);
	FF_ASSERT(v->store == NULL);

	if (pos == -1)
		pos = gtk_tree_view_get_n_columns(GTK_TREE_VIEW(v->h));

	GtkTreeViewColumn *col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, vc->text);
	gtk_tree_view_column_set_resizable(col, 1);
	if (vc->width != 0)
		gtk_tree_view_column_set_fixed_width(col, vc->width);

	gtk_tree_view_column_pack_start(col, v->rend, 1);
	gtk_tree_view_column_add_attribute(col, v->rend, "text", pos);

	gtk_tree_view_insert_column(GTK_TREE_VIEW(v->h), col, pos);
	ffui_viewcol_reset(vc);
}

void ffui_view_setcol(ffui_view *v, int pos, ffui_viewcol *vc)
{
	GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(v->h), pos);
	if (vc->text != NULL)
		gtk_tree_view_column_set_title(col, vc->text);
	if (vc->width != 0)
		gtk_tree_view_column_set_fixed_width(col, vc->width);
}

void ffui_view_col(ffui_view *v, int pos, ffui_viewcol *vc)
{
	GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(v->h), pos);
	vc->width = gtk_tree_view_column_get_width(col);
}

static void view_prepare(ffui_view *v)
{
	uint ncol = gtk_tree_view_get_n_columns(GTK_TREE_VIEW(v->h));
	GType *types = ffmem_allocT(ncol, GType);
	for (uint i = 0;  i != ncol;  i++) {
		types[i] = G_TYPE_STRING;
	}
	v->store = (void*)gtk_list_store_newv(ncol, types);
	ffmem_free(types);
	gtk_tree_view_set_model(GTK_TREE_VIEW(v->h), v->store);
	g_object_unref(v->store);
}

ffui_sel* ffui_view_getsel(ffui_view *v)
{
	void *tvsel = gtk_tree_view_get_selection(GTK_TREE_VIEW(v->h));
	GList *rows = gtk_tree_selection_get_selected_rows(tvsel, NULL);
	uint n = gtk_tree_selection_count_selected_rows(tvsel);

	ffvec a = {};
	if (NULL == ffvec_allocT(&a, n, uint))
		return NULL;

	for (GList *l = rows;  l != NULL;  l = l->next) {
		GtkTreePath *path = l->data;
		int *ii = gtk_tree_path_get_indices(path);
		*ffvec_pushT(&a, uint) = ii[0];
	}
	g_list_free_full(rows, (GDestroyNotify)gtk_tree_path_free);

	ffui_sel *sel = ffmem_new(ffui_sel);
	sel->ptr = a.ptr;
	sel->len = a.len;
	sel->off = 0;
	return sel;
}

void ffui_view_dragdrop(ffui_view *v, uint action_id)
{
	if (v->store == NULL)
		view_prepare(v);

	static const GtkTargetEntry ents[] = {
		{ "text/plain", GTK_TARGET_OTHER_APP, 0 }
	};
	gtk_drag_dest_set(v->h, GTK_DEST_DEFAULT_ALL, ents, FF_COUNT(ents), GDK_ACTION_COPY);
	g_signal_connect(v->h, "drag_data_received", G_CALLBACK(_ffui_view_drag_data_received), v);
	v->dropfile_id = action_id;
}

void ffui_view_ins(ffui_view *v, int pos, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "pos:%d", pos);
	if (v->store == NULL)
		view_prepare(v);

	GtkTreeIter iter;
	if (pos == -1) {
		it->idx = ffui_view_nitems(v);
		gtk_list_store_append(GTK_LIST_STORE(v->store), &iter);
	} else {
		it->idx = pos;
		gtk_list_store_insert(GTK_LIST_STORE(v->store), &iter, pos);
	}
	gtk_list_store_set(GTK_LIST_STORE(v->store), &iter, 0, it->text, -1);
	ffui_view_itemreset(it);
}

void ffui_view_set(ffui_view *v, int sub, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "sub:%d", sub);
	GtkTreeIter iter;
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(v->store), &iter, NULL, it->idx))
		gtk_list_store_set(GTK_LIST_STORE(v->store), &iter, sub, it->text, -1);
	ffui_view_itemreset(it);
}

void ffui_view_rm(ffui_view *v, ffui_viewitem *it)
{
	FFDBG_PRINTLN(10, "idx:%d", it->idx);
	GtkTreeIter iter;
	if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(v->store), &iter, NULL, it->idx))
		gtk_list_store_remove(GTK_LIST_STORE(v->store), &iter);
}

void ffui_view_setdata(ffui_view *v, uint first, int delta)
{
	if (v->store == NULL)
		view_prepare(v);

	FF_ASSERT(v->dispinfo_id != 0);
	uint cols = ffui_view_ncols(v);
	if (cols == 0)
		return;

	uint rows = ffui_view_nitems(v);
	int n = first + delta;
	if (delta == 0 && rows != 0)
		n++; // redraw the item

	FFDBG_PRINTLN(10, "first:%u  delta:%d  rows:%u", first, delta, rows);

	if (first > rows)
		return;

	char buf[1024];
	ffui_viewitem it = {};
	for (uint i = first;  (int)i < n;  i++) {

		v->disp.idx = i;

		ffstr_set(&v->disp.text, buf, sizeof(buf) - 1);
		buf[0] = '\0';
		v->disp.sub = 0;
		v->wnd->on_action(v->wnd, v->dispinfo_id);

		ffui_view_setindex(&it, i);
		buf[v->disp.text.len] = '\0';
		ffui_view_settextz(&it, buf);
		int ins = 0;
		if (i >= rows) {
			ffui_view_ins(v, -1, &it);
			rows++;
			ins = 1;
		} else if (delta == 0) {
			ffui_view_set(v, 0, &it); // redraw
		} else {
			ffui_view_ins(v, i, &it);
			rows++;
			ins = 1;
		}
		// FFDBG_PRINTLN(0, "idx:%u  text:%s", i, buf);

		for (uint c = 1;  c != cols;  c++) {
			ffstr_set(&v->disp.text, buf, sizeof(buf) - 1);
			buf[0] = '\0';
			v->disp.sub = c;
			v->wnd->on_action(v->wnd, v->dispinfo_id);
			if (ins && buf[0] == '\0')
				continue;

			ffui_view_setindex(&it, i);
			buf[v->disp.text.len] = '\0';
			ffui_view_settextz(&it, buf);
			ffui_view_set(v, c, &it);
			// FFDBG_PRINTLN(0, "idx:%u  text:%s", i, buf);
		}
	}

	int i = first;
	while (i > (int)n) {
		ffui_view_setindex(&it, i);
		ffui_view_rm(v, &it);
		// FFDBG_PRINTLN(0, "removed idx:%u", i);
		i--;
	}
}


// STATUSBAR

int ffui_stbar_create(ffui_ctl *sb, ffui_wnd *parent)
{
	sb->h = gtk_statusbar_new();
	gtk_box_pack_end(GTK_BOX(parent->vbox), sb->h, /*expand=*/0, /*fill=*/0, /*padding=*/0);
	return 0;
}


// TRAYICON
static void _ffui_tray_activate(GtkWidget *mi, gpointer udata)
{
	ffui_trayicon *t = udata;
	t->wnd->on_action(t->wnd, t->lclick_id);
}

int ffui_tray_create(ffui_trayicon *t, ffui_wnd *wnd)
{
	t->h = (void*)gtk_status_icon_new();
	t->wnd = wnd;
	g_signal_connect(t->h, "activate", G_CALLBACK(&_ffui_tray_activate), t);
	ffui_tray_show(t, 0);
	return 0;
}


// DIALOG
char* ffui_dlg_open(ffui_dialog *d, ffui_wnd *parent)
{
	g_free(d->name);  d->name = NULL;

	GtkWidget *dlg;
	dlg = gtk_file_chooser_dialog_new(d->title, parent->h, GTK_FILE_CHOOSER_ACTION_OPEN
		, "_Cancel", GTK_RESPONSE_CANCEL
		, "_Open", GTK_RESPONSE_ACCEPT
		, NULL);
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dlg);

	if (d->multisel)
		gtk_file_chooser_set_select_multiple(chooser, 1);

	int r = gtk_dialog_run(GTK_DIALOG(dlg));
	if (r == GTK_RESPONSE_ACCEPT) {
		if (d->multisel)
			d->curname = d->names = gtk_file_chooser_get_filenames(chooser);
		else
			d->name = gtk_file_chooser_get_filename(chooser);
	}

	gtk_widget_destroy(dlg);
	if (d->names != NULL)
		return ffui_dlg_nextname(d);
	return d->name;
}

char* ffui_dlg_nextname(ffui_dialog *d)
{
	if (d->curname == NULL)
		return NULL;
	char *name = d->curname->data;
	d->curname = d->curname->next;
	return name;
}

char* ffui_dlg_save(ffui_dialog *d, ffui_wnd *parent, const char *fn, size_t fnlen)
{
	ffmem_free0(d->name);
	GtkWidget *dlg;
	dlg = gtk_file_chooser_dialog_new(d->title, parent->h, GTK_FILE_CHOOSER_ACTION_SAVE
		, "_Cancel", GTK_RESPONSE_CANCEL
		, "_Save", GTK_RESPONSE_ACCEPT
		, NULL);
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dlg);

	gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

	char *sz = ffsz_dupn(fn, fnlen);
	// gtk_file_chooser_set_filename (chooser, sz);
	gtk_file_chooser_set_current_name(chooser, sz);

	int r = gtk_dialog_run(GTK_DIALOG(dlg));
	if (r == GTK_RESPONSE_ACCEPT) {
		d->name = gtk_file_chooser_get_filename(chooser);
	}

	gtk_widget_destroy(dlg);
	return d->name;
}


// WINDOW
static void _ffui_wnd_onclose(void *a, void *b, gpointer udata)
{
	ffui_wnd *wnd = udata;
	if (wnd->hide_on_close) {
		ffui_show(wnd, 0);
		return;
	}
	wnd->on_action(wnd, wnd->onclose_id);
}

int ffui_wnd_create(ffui_wnd *w)
{
	w->uid = FFUI_UID_WINDOW;
	w->h = (void*)gtk_window_new(GTK_WINDOW_TOPLEVEL);
	w->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(w->h), w->vbox);
	g_object_set_data(G_OBJECT(w->h), "ffdata", w);
	g_signal_connect(w->h, "delete-event", G_CALLBACK(&_ffui_wnd_onclose), w);
	return 0;
}

static gboolean _ffui_wnd_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event->keyval == GDK_KEY_Escape) {
		_ffui_wnd_onclose(widget, event, data);
		return 1;
	}
	return 0;
}

void ffui_wnd_setpopup(ffui_wnd *w, ffui_wnd *parent)
{
	gtk_window_set_transient_for(w->h, parent->h);
	g_signal_connect(w->h, "key_press_event", G_CALLBACK(_ffui_wnd_key_press_event), w);
}

static const ushort _ffui_ikeys[] = {
	GDK_KEY_BackSpace,
	GDK_KEY_Break,
	GDK_KEY_Delete,
	GDK_KEY_Down,
	GDK_KEY_End,
	GDK_KEY_Return,
	GDK_KEY_Escape,
	GDK_KEY_F1,
	GDK_KEY_F10,
	GDK_KEY_F11,
	GDK_KEY_F12,
	GDK_KEY_F2,
	GDK_KEY_F3,
	GDK_KEY_F4,
	GDK_KEY_F5,
	GDK_KEY_F6,
	GDK_KEY_F7,
	GDK_KEY_F8,
	GDK_KEY_F9,
	GDK_KEY_Home,
	GDK_KEY_Insert,
	GDK_KEY_Left,
	GDK_KEY_Right,
	GDK_KEY_space,
	GDK_KEY_Tab,
	GDK_KEY_Up,
};

static const char *const _ffui_keystr[] = {
	"backspace",
	"break",
	"delete",
	"down",
	"end",
	"enter",
	"escape",
	"f1",
	"f10",
	"f11",
	"f12",
	"f2",
	"f3",
	"f4",
	"f5",
	"f6",
	"f7",
	"f8",
	"f9",
	"home",
	"insert",
	"left",
	"right",
	"space",
	"tab",
	"up",
};

ffui_hotkey ffui_hotkey_parse(const char *s, size_t len)
{
	int r = 0, f;
	const char *end = s + len;
	ffstr v;
	enum {
		fctrl = GDK_CONTROL_MASK << 16,
		fshift = GDK_SHIFT_MASK << 16,
		falt = GDK_MOD1_MASK << 16,
	};

	if (s == end)
		goto fail;

	while (s != end) {
		s += ffstr_nextval(s, end - s, &v, '+');

		if (ffstr_ieqcz(&v, "ctrl"))
			f = fctrl;
		else if (ffstr_ieqcz(&v, "alt"))
			f = falt;
		else if (ffstr_ieqcz(&v, "shift"))
			f = fshift;
		else {
			if (s != end)
				goto fail; //the 2nd key is an error
			break;
		}

		if (r & f)
			goto fail;
		r |= f;
	}

	if (v.len == 1
		&& (ffchar_isletter(v.ptr[0])
			|| ffchar_isdigit(v.ptr[0])
			|| v.ptr[0] == '[' || v.ptr[0] == ']'
			|| v.ptr[0] == '`'
			|| v.ptr[0] == '/' || v.ptr[0] == '\\'))
		r |= v.ptr[0];

	else {
		ssize_t ikey = ffszarr_ifindsorted(_ffui_keystr, FF_COUNT(_ffui_keystr), v.ptr, v.len);
		if (ikey == -1)
			goto fail; //unknown key
		r |= _ffui_ikeys[ikey];
	}

	return r;

fail:
	return 0;
}

int ffui_wnd_hotkeys(ffui_wnd *w, const ffui_wnd_hotkey *hotkeys, size_t n)
{
	GtkAccelGroup *ag = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(w->h), ag);

	for (uint i = 0;  i != n;  i++) {
		gtk_widget_add_accelerator(hotkeys[i].h, "activate", ag
			, ffui_hotkey_key(hotkeys[i].hk), ffui_hotkey_mod(hotkeys[i].hk), GTK_ACCEL_VISIBLE);
	}

	return 0;
}


// MESSAGE LOOP

struct cmd {
	ffui_handler func;
	void *udata;
	uint id;
	uint ref;
};

static gboolean _ffui_thd_func(gpointer data)
{
	struct cmd *c = data;
	FFDBG_PRINTLN(10, "func:%p  udata:%p", c->func, c->udata);
	c->func(c->udata);
	if (c->ref != 0) {
		ffatom_fence_acq_rel();
		FF_WRITEONCE(c->ref, 0);
	} else
		ffmem_free(c);
	return 0;
}

void ffui_thd_post(ffui_handler func, void *udata, uint id)
{
	FFDBG_PRINTLN(10, "func:%p  udata:%p  id:%xu", func, udata, id);

	if (id & FFUI_POST_WAIT) {
		struct cmd c;
		c.func = func;
		c.udata = udata;
		c.ref = 1;
		ffatom_fence_rel();

		if (0 != gdk_threads_add_idle(&_ffui_thd_func, &c)) {
			ffatom_waitchange(&c.ref, 1);
		}
		return;
	}

	struct cmd *c = ffmem_new(struct cmd);
	c->func = func;
	c->udata = udata;

	if (0 != gdk_threads_add_idle(&_ffui_thd_func, c)) {
	}
}

struct cmd_send {
	void *ctl;
	void *udata;
	uint id;
	uint ref;
};

static gboolean _ffui_send_handler(gpointer data)
{
	struct cmd_send *c = data;
	switch ((enum FFUI_MSG)c->id) {

	case FFUI_QUITLOOP:
		ffui_quitloop();
		break;

	case FFUI_LBL_SETTEXT:
		ffui_lbl_settextz((ffui_label*)c->ctl, c->udata);
		break;

	case FFUI_EDIT_GETTEXT:
		ffui_edit_textstr((ffui_edit*)c->ctl, c->udata);
		break;

	case FFUI_TEXT_SETTEXT:
		ffui_text_clear((ffui_text*)c->ctl);
		ffui_text_addtextstr((ffui_text*)c->ctl, (ffstr*)c->udata);
		break;
	case FFUI_TEXT_ADDTEXT:
		ffui_text_addtextstr((ffui_text*)c->ctl, (ffstr*)c->udata);
		break;
	case FFUI_WND_SETTEXT:
		ffui_wnd_settextz((ffui_wnd*)c->ctl, c->udata);
		break;
	case FFUI_CHECKBOX_SETTEXTZ:
		ffui_checkbox_settextz((ffui_checkbox*)c->ctl, c->udata);
		break;
	case FFUI_WND_SHOW:
		ffui_show((ffui_wnd*)c->ctl, (ffsize)c->udata);
		break;

	case FFUI_VIEW_RM:
		ffui_view_rm((ffui_view*)c->ctl, c->udata);
		break;
	case FFUI_VIEW_CLEAR:
		ffui_view_clear((ffui_view*)c->ctl);
		break;
	case FFUI_VIEW_SCROLLSET:
		ffui_view_scroll_setvert((ffui_view*)c->ctl, (size_t)c->udata);
		break;
	case FFUI_VIEW_GETSEL:
		c->udata = ffui_view_getsel((ffui_view*)c->ctl);
		break;
	case FFUI_VIEW_SETDATA: {
		uint first = (size_t)c->udata >> 16;
		uint delta = (ssize_t)c->udata & 0xffff;
		ffui_view_setdata((ffui_view*)c->ctl, first, (short)delta);
		break;
	}

	case FFUI_TRK_SETRANGE:
		ffui_trk_setrange((ffui_trkbar*)c->ctl, (size_t)c->udata);
		break;
	case FFUI_TRK_SET:
		ffui_trk_set((ffui_trkbar*)c->ctl, (size_t)c->udata);
		break;

	case FFUI_TAB_INS:
		ffui_tab_append((ffui_tab*)c->ctl, c->udata);
		break;
	case FFUI_TAB_SETACTIVE:
		ffui_tab_setactive((ffui_tab*)c->ctl, (ffsize)c->udata);
		break;
	case FFUI_TAB_ACTIVE:
		*(ffsize*)c->udata = ffui_tab_active((ffui_tab*)c->ctl);
		break;
	case FFUI_TAB_COUNT:
		*(ffsize*)c->udata = ffui_tab_count((ffui_tab*)c->ctl);
		break;

	case FFUI_STBAR_SETTEXT:
		ffui_stbar_settextz((ffui_ctl*)c->ctl, c->udata);
		break;

	case FFUI_CLIP_SETTEXT: {
		GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
		ffstr *s = c->udata;
		gtk_clipboard_set_text(clip, s->ptr, s->len);
		break;
	}
	}

	if (c->ref != 0) {
		ffatom_fence_acq_rel();
		FF_WRITEONCE(c->ref, 0);
	} else {
		ffmem_free(c);
	}
	return 0;
}

static uint quit;
static fflock quit_lk;

static int post_locked(gboolean (*func)(gpointer), void *udata)
{
	int r = 0;
	fflk_lock(&quit_lk);
	if (!quit)
		r = gdk_threads_add_idle(func, udata);
	fflk_unlock(&quit_lk);
	return r;
}

size_t ffui_send(void *ctl, uint id, void *udata)
{
	FFDBG_PRINTLN(10, "ctl:%p  udata:%p  id:%xu", ctl, udata, id);
	struct cmd_send c;
	c.ctl = ctl;
	c.id = id;
	c.udata = udata;
	c.ref = 1;
	ffatom_fence_rel();

	if (ffthread_curid() == _ffui_thd_id) {
		return _ffui_send_handler(&c);
	}

	if (0 != post_locked(&_ffui_send_handler, &c)) {
		ffatom_waitchange(&c.ref, 1);
		return (size_t)c.udata;
	}
	return 0;
}

void ffui_post(void *ctl, uint id, void *udata)
{
	FFDBG_PRINTLN(10, "ctl:%p  udata:%p  id:%xu", ctl, udata, id);
	struct cmd_send *c = ffmem_new(struct cmd_send);
	c->ctl = ctl;
	c->id = id;
	c->udata = udata;
	ffatom_fence_rel();

	if (id == FFUI_QUITLOOP) {
		fflk_lock(&quit_lk);
		if (0 == gdk_threads_add_idle(&_ffui_send_handler, c))
			ffmem_free(c);
		quit = 1;
		fflk_unlock(&quit_lk);
		return;
	}

	if (ffthread_curid() == _ffui_thd_id) {
		_ffui_send_handler(c);
		return;
	}

	if (0 == post_locked(&_ffui_send_handler, c))
		ffmem_free(c);
}
