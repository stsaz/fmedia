/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>


enum {
	VSETS_NAME,
	VSETS_VAL,
	VSETS_DESC,
};

enum {
	VINFO_NAME,
	VINFO_VAL,
};


static void gui_cvt_action(ffui_wnd *wnd, int id);
static void gui_conv_browse(void);
static void gui_convert(void);

static void gui_info_action(ffui_wnd *wnd, int id);


void wconvert_init()
{
	gg->wconvert.wconvert.hide_on_close = 1;
	gg->wconvert.wconvert.on_action = &gui_cvt_action;
	gg->wconvert.vsets.edit_id = CVT_SETS_EDITDONE;
}

static const struct cmd cvt_cmds[] = {
	{ OUTBROWSE,	F0,	&gui_conv_browse },
	{ CONVERT,	F0,	&gui_convert },
};

static void gui_cvt_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, cvt_cmds, FFCNT(cvt_cmds));
	if (cmd != NULL) {
		cmdfunc_u u;
		u.f = cmd->func;
		if (cmd->flags & F0)
			u.f0();
		else
			u.f(id);
		return;
	}

	switch (id) {
	case CVT_SETS_EDIT: {
		int i = ffui_view_selnext(&gg->wconvert.vsets, -1);
		ffui_view_edit(&gg->wconvert.vsets, i, VSETS_VAL);
		}
		break;

	case CVT_SETS_EDITDONE: {
		int i = ffui_view_selnext(&gg->wconvert.vsets, -1);
		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_settextz(&it, gg->wconvert.vsets.text);
		ffui_view_set(&gg->wconvert.vsets, VSETS_VAL, &it);
		}
		break;
	}
}

struct cvt_set {
	const char *settname;
	const char *name;
	const char *defval;
	const char *desc;
};

static const struct cvt_set cvt_sets[] = {
	{ "ogg-quality", "OGG Vorbis Quality", "5.0", "-1.0 .. 10.0" },
	{ "mpeg-quality", "MPEG Quality", "2", "VBR quality: 9..0 or CBR bitrate: 64..320" },
	{ "flac_complevel", "FLAC Compression", "6", "0..8" },
	{ "overwrite", "Overwrite Output File", "0", "0 or 1" },
	{ "out_preserve_date", "Preserve Date", "1", "0 or 1" },
};

void gui_showconvert(void)
{
	if (0 == ffui_view_selcount(&gg->wmain.vlist))
		return;

	if (!gg->wconv_init) {
		ffui_viewitem it;
		ffui_view_iteminit(&it);

		uint i;
		for (i = 0;  i != FFCNT(cvt_sets);  i++) {
			ffui_view_settextz(&it, cvt_sets[i].name);
			ffui_view_append(&gg->wconvert.vsets, &it);

			ffui_view_settextz(&it, cvt_sets[i].defval);
			ffui_view_set(&gg->wconvert.vsets, VSETS_VAL, &it);

			ffui_view_settextz(&it, cvt_sets[i].desc);
			ffui_view_set(&gg->wconvert.vsets, VSETS_DESC, &it);
		}

		gg->wconv_init = 1;
	}

	ffui_show(&gg->wconvert.wconvert, 1);
	ffui_wnd_setfront(&gg->wconvert.wconvert);
}

static void gui_convert(void)
{
	int i = -1;
	ffui_viewitem it;
	fmed_que_entry e, *qent, *inp;
	ffstr fn;
	void *play = NULL;

	ffui_view_iteminit(&it);
	ffui_textstr(&gg->wconvert.eout, &fn);
	if (fn.len == 0)
		return;

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
		inp = (void*)ffui_view_param(&it);

		ffmemcpy(&e, inp, sizeof(fmed_que_entry));
		if (NULL == (qent = (void*)gg->qu->cmd2(FMED_QUE_ADD | FMED_QUE_NO_ONCHANGE, &e, 0))) {
			continue;
		}
		gui_media_added(qent);

		gg->qu->meta_set(qent, FFSTR("output"), fn.ptr, fn.len, FMED_QUE_TRKDICT);
		if (play == NULL)
			play = qent;

		uint k;
		int64 val;
		char *txt;
		size_t len;
		ffstr name;
		ffui_view_iteminit(&it);
		for (k = 0;  k != FFCNT(cvt_sets);  k++) {

			ffstr_setz(&name, cvt_sets[k].settname);

			ffui_view_setindex(&it, k);
			ffui_view_gettext(&it);
			ffui_view_get(&gg->wconvert.vsets, VSETS_VAL, &it);

			if (NULL == (txt = ffsz_alcopyqz(ffui_view_textq(&it)))) {
				syserrlog(core, NULL, "gui", "%e", FFERR_BUFALOC);
				goto end;
			}
			len = ffsz_len(txt);

			if (ffstr_eqcz(&name, "ogg-quality")) {
				double d;
				ffs_tofloat(txt, len, &d, 0);
				val = d * 10;
			} else {
				ffs_toint(txt, len, &val, FFS_INT64);
			}

			ffmem_free(txt);

			gg->qu->meta_set(qent, name.ptr, name.len
				, (char*)&val, sizeof(int64), FMED_QUE_TRKDICT | FMED_QUE_NUM);
		}

		ffui_view_itemreset(&it);
	}

	if (play != NULL) {
		gg->play_id = play;
		gui_task_add(PLAY);
	}
end:
	ffui_view_itemreset(&it);
	ffstr_free(&fn);
}

static void gui_conv_browse(void)
{
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_OUTPUT);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &gg->wconvert.wconvert, NULL, 0)))
		return;

	ffui_settextz(&gg->wconvert.eout, fn);
}


void winfo_init()
{
	gg->winfo.winfo.hide_on_close = 1;
	gg->winfo.winfo.on_action = &gui_info_action;
}

static void gui_info_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case INFOEDIT: {
		int i = ffui_view_selnext(&gg->winfo.vinfo, -1);
		ffui_view_edit(&gg->winfo.vinfo, i, VINFO_VAL);
		}
		break;
	}
}

void gui_media_showinfo(void)
{
	fmed_que_entry *e;
	ffui_viewitem it;
	int i;
	ffstr name, *val;

	ffui_show(&gg->winfo.winfo, 1);

	if (-1 == (i = ffui_view_selnext(&gg->wmain.vlist, -1))) {
		ffui_view_clear(&gg->winfo.vinfo);
		return;
	}

	ffui_view_iteminit(&it);
	ffui_view_setindex(&it, i);
	ffui_view_setparam(&it, 0);
	ffui_view_get(&gg->wmain.vlist, 0, &it);
	e = (void*)ffui_view_param(&it);

	ffui_settextstr(&gg->winfo.winfo, &e->url);

	ffui_redraw(&gg->winfo.vinfo, 0);
	ffui_view_clear(&gg->winfo.vinfo);
	for (i = 0;  NULL != (val = gg->qu->meta(e, i, &name, 0));  i++) {
		ffui_view_iteminit(&it);
		ffui_view_settextstr(&it, &name);
		ffui_view_append(&gg->winfo.vinfo, &it);

		ffui_view_settextstr(&it, val);
		ffui_view_set(&gg->winfo.vinfo, 1, &it);
	}
	ffui_redraw(&gg->winfo.vinfo, 1);
}


void wabout_init(void)
{
	ffui_settextz(&gg->wabout.labout, "fmedia v" FMED_VER "\nhttp://fmedia.firmdev.com");
	gg->wabout.wabout.hide_on_close = 1;
}
