/** fmedia: GUI: display file meta info
2020, Simon Zolin */

struct gui_winfo {
	ffui_wnd winfo;
	ffui_view vinfo;
};

const ffui_ldr_ctl winfo_ctls[] = {
	FFUI_LDR_CTL(struct gui_winfo, winfo),
	FFUI_LDR_CTL(struct gui_winfo, vinfo),
	FFUI_LDR_CTL_END
};

void winfo_action(ffui_wnd *wnd, int id)
{
}

void winfo_init()
{
	struct gui_winfo *w = ffmem_new(struct gui_winfo);
	gg->winfo = w;
	w->winfo.hide_on_close = 1;
	w->winfo.on_action = &winfo_action;
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

/** Show info about a playlist's item. */
void winfo_show(uint show, uint idx)
{
	struct gui_winfo *w = gg->winfo;

	if (!show) {
		ffui_show(&w->winfo, 0);
		return;
	}

	ffstr name, empty = {}, *val;
	ffvec data = {};
	fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item_locked(-1, idx);
	if (ent == NULL)
		return;

	ffui_wnd_settextz(&w->winfo, ent->url.ptr);
	ffui_view_clear(&w->vinfo);

	ffstr_setz(&name, "File path");
	winfo_addpair(&name, &ent->url);

	fffileinfo fi = {};
	ffbool have_fi = (0 == fffile_infofn(ent->url.ptr, &fi));
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
	if (NULL == (val = gg->qu->meta_find(ent, FFSTR("__info"))))
		val = &empty;
	winfo_addpair(&name, val);

	for (uint i = 0;  NULL != (val = gg->qu->meta(ent, i, &name, 0));  i++) {

		if (val == FMED_QUE_SKIP)
			continue;

		winfo_addpair(&name, val);
	}

	gg->qu->cmdv(FMED_QUE_ITEMUNLOCK, ent);

	ffui_show(&w->winfo, 1);

	ffvec_free(&data);
}
