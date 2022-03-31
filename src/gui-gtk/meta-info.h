/** fmedia: GUI: display file meta info
2020, Simon Zolin */

struct gui_winfo {
	ffui_wnd winfo;
	ffui_view vinfo;
	ffui_menu mminfo;
	uint list_idx;
	ffvec tag_names; // char*[]
};

void winfo_update(fmed_que_entry *qe);

#define META_OFF  4

const ffui_ldr_ctl winfo_ctls[] = {
	FFUI_LDR_CTL(struct gui_winfo, winfo),
	FFUI_LDR_CTL(struct gui_winfo, vinfo),
	FFUI_LDR_CTL(struct gui_winfo, mminfo),
	FFUI_LDR_CTL_END
};

void winfo_edit(int idx, const char *new_text)
{
	struct gui_winfo *w = gg->winfo;
	int ok = 0;
	ffstr text = FFSTR_INITZ(new_text);

	idx -= META_OFF;
	if (idx < 0)
		return;

	fmed_que_entry *qe = (void*)gg->qu->fmed_queue_item_locked(-1, w->list_idx);
	if (qe == NULL)
		return;

	if ((uint)idx >= w->tag_names.len)
		goto end;
	const char *namez = *ffslice_itemT(&w->tag_names, idx, char*);
	ffstr name = FFSTR_INITZ(namez);
	gg->qu->meta_set2(qe, name, text, FMED_QUE_OVWRITE | FMED_QUE_NOLOCK);
	ok = 1;

end:
	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, qe);
	if (ok) {
		ffui_view_set_i_textz(&w->vinfo, idx + META_OFF, 1, new_text);
	}
}

void winfo_write__tcore(void *param)
{
	struct gui_winfo *w = gg->winfo;
	const fmed_edittags *et = core->getmod("fmt.edit-tags");
	if (et == NULL)
		return;
	ffvec m = {};
	struct fmed_edittags_conf etc = {};
	etc.preserve_date = 1;

	fmed_que_entry *qe = (void*)gg->qu->fmed_queue_item(-1, w->list_idx);
	if (qe == NULL)
		return;
	etc.fn = ffsz_dupstr(&qe->url);

	for (uint i = 0;  ;  i++) {
		ffstr name, *val;
		if (NULL == (val = gg->qu->meta(qe, i, &name, FMED_QUE_UNIQ)))
			break;
		if (val == FMED_QUE_SKIP)
			continue;
		ffvec_addfmt(&m, "%S=%S;", &name, val);
	}
	ffstr_setstr(&etc.meta, &m);

	et->edit(&etc);
	ffvec_free(&m);
	ffmem_free((char*)etc.fn);
}

void winfo_addtag(int id)
{
	struct gui_winfo *w = gg->winfo;
	ffstr name;
	switch (id) {
	case A_INFO_ADD_ARTIST:
		ffstr_setz(&name, "artist"); break;
	case A_INFO_ADD_TITLE:
		ffstr_setz(&name, "title"); break;
	default:
		return;
	}

	fmed_que_entry *qe = (void*)gg->qu->fmed_queue_item_locked(-1, w->list_idx);
	if (qe == NULL)
		return;

	ffstr text = {};
	gg->qu->meta_set2(qe, name, text, FMED_QUE_OVWRITE | FMED_QUE_NOLOCK);

	winfo_update(qe);

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, qe);
}

void winfo_action(ffui_wnd *wnd, int id)
{
	struct gui_winfo *w = gg->winfo;
	switch (id) {
	case A_INFO_EDIT:
		winfo_edit(w->vinfo.edited.idx, w->vinfo.edited.new_text);
		break;

	case A_INFO_WRITE:
		corecmd_addfunc(winfo_write__tcore, NULL);
		break;

	default:
		winfo_addtag(id);
	}
}

void winfo_init()
{
	struct gui_winfo *w = ffmem_new(struct gui_winfo);
	gg->winfo = w;
	w->winfo.hide_on_close = 1;
	w->winfo.on_action = &winfo_action;
	w->vinfo.edit_id = A_INFO_EDIT;
}

void winfo_destroy()
{
	struct gui_winfo *w = gg->winfo;
	char **it;
	FFSLICE_WALK(&w->tag_names, it) {
		ffmem_free(*it);
	}
	ffvec_free(&w->tag_names);
	ffmem_free(w);
}

void winfo_addpair(const ffstr *name, const ffstr *val)
{
	struct gui_winfo *w = gg->winfo;
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffui_view_settextstr(&it, name);
	ffui_view_append(&w->vinfo, &it);

	ffui_view_settextstr(&it, val);
	ffui_view_set(&w->vinfo, 1, &it);
}

void winfo_update(fmed_que_entry *qe)
{
	ffstr name, empty = {}, *val;
	ffvec data = {};

	struct gui_winfo *w = gg->winfo;
	ffui_view_clear(&w->vinfo);

	ffstr_setz(&name, "File path");
	winfo_addpair(&name, &qe->url);

	fffileinfo fi = {};
	ffbool have_fi = (0 == fffile_infofn(qe->url.ptr, &fi));
	ffvec_alloc(&data, 255, 1);

	ffstr_setz(&name, "File size");
	data.len = 0;
	if (have_fi)
		ffvec_addfmt(&data, "%U KB", fffile_infosize(&fi) / 1024);
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "File date");
	data.len = 0;
	if (have_fi) {
		ffdatetime dt;
		fftime t = fffile_infomtime(&fi);
		uint tzoff = core->cmd(FMED_TZOFFSET);
		t.sec += FFTIME_1970_SECONDS + tzoff;
		fftime_split1(&dt, &t);
		data.len = fftime_tostr1(&dt, data.ptr, data.cap, FFTIME_DATE_WDMY | FFTIME_HMS);
	}
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "Info");
	if (NULL == (val = gg->qu->meta_find(qe, FFSTR("__info"))))
		val = &empty;
	winfo_addpair(&name, val);

	char **it;
	FFSLICE_WALK(&w->tag_names, it) {
		ffmem_free(*it);
	}
	ffvec_free(&w->tag_names);

	for (uint i = 0;  ;  i++) {
		if (NULL == (val = gg->qu->meta(qe, i, &name, 0)))
			break;
		if (val == FMED_QUE_SKIP)
			continue;

		*ffvec_pushT(&w->tag_names, char*) = ffsz_dupstr(&name);
		winfo_addpair(&name, val);
	}

	ffvec_free(&data);
}

/** Show info about a playlist's item. */
void winfo_show(uint show, uint idx)
{
	struct gui_winfo *w = gg->winfo;

	if (!show) {
		ffui_show(&w->winfo, 0);
		return;
	}

	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, idx);
	if (ent == NULL)
		return;

	ffui_wnd_settextz(&w->winfo, ent->url.ptr);
	winfo_update(ent);

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);
	w->list_idx = idx;

	ffui_show(&w->winfo, 1);
}
