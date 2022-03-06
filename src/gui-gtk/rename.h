/** fmedia: gui-gtk: rename file
2021, Simon Zolin */

#include <util/path.h>

struct gui_wrename {
	ffui_wnd wnd;
	ffui_edit tname;
	ffui_btn brename;

	fmed_que_entry *ent;
	int idx;
};

const ffui_ldr_ctl wrename_ctls[] = {
	FFUI_LDR_CTL(struct gui_wrename, wnd),
	FFUI_LDR_CTL(struct gui_wrename, tname),
	FFUI_LDR_CTL(struct gui_wrename, brename),
	FFUI_LDR_CTL_END
};

void wrename_action(ffui_wnd *wnd, int id)
{
	struct gui_wrename *w = gg->wrename;

	switch (id) {
	case A_RENAME: {
		ffstr fn = {};
		char *newfn = NULL;
		ffui_edit_textstr(&w->tname, &fn);

		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, w->idx);
		if (ent == NULL)
			goto end;
		if (ent != w->ent)
			goto end;

		newfn = ffsz_dupn(fn.ptr, fn.len);
		int k = 0;

		if (fffile_exists(newfn)) {
			errlog("file rename: %s -> %s: target file already exists", ent->url.ptr, newfn);
			goto end;
		}

		if (0 != fffile_rename(ent->url.ptr, newfn))
			syserrlog("file rename: %s -> %s", ent->url.ptr, newfn);
		else {
			dbglog("file rename: %s -> %s", ent->url.ptr, newfn);
			k = 1;
		}

end:
		ffmem_free(newfn);
		ffstr_free(&fn);
		gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);
		if (k)
			ffui_show(&w->wnd, 0);
		break;
	}
	}
}

void wrename_show(uint show)
{
	struct gui_wrename *w = gg->wrename;

	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}

	ffui_sel *sel = wmain_list_getsel();
	int i = ffui_view_selnext(NULL, sel);
	if (i == -1)
		goto end;

	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, i);
	if (ent == NULL)
		return;

	ffui_edit_settextz(&w->tname, ent->url.ptr);
	ffstr name;
	ffpath_split3(ent->url.ptr, ent->url.len, NULL, &name, NULL);
	ffui_edit_sel(&w->tname, name.ptr-ent->url.ptr, ffstr_end(&name)-ent->url.ptr);

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);

	w->ent = ent;
	w->idx = i;
	ffui_show(&w->wnd, 1);

end:
	ffui_view_sel_free(sel);
}

void wrename_init()
{
	struct gui_wrename *w = ffmem_new(struct gui_wrename);
	gg->wrename = w;
	w->wnd.hide_on_close = 1;
	w->wnd.on_action = wrename_action;
}
