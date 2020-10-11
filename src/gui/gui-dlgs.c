/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>

#include <FF/time.h>
#include <FFOS/process.h>


enum {
	VINFO_NAME,
	VINFO_VAL,
};


static void gui_dev_action(ffui_wnd *wnd, int id);

static void gui_info_action(ffui_wnd *wnd, int id);

static void gui_wgoto_action(ffui_wnd *wnd, int id);

static void gui_wabout_action(ffui_wnd *wnd, int id);

static void gui_wuri_action(ffui_wnd *wnd, int id);

static void gui_wfilter_action(ffui_wnd *wnd, int id);


void wdev_init(void)
{
	gg->wdev.wnd.hide_on_close = 1;
	gg->wdev.wnd.on_action = &gui_dev_action;
}

enum {
	VDEV_ID,
	VDEV_NAME,
};

void gui_dev_show(uint cmd)
{
	ffui_viewitem it = {0};
	fmed_adev_ent *ents = NULL;
	const fmed_modinfo *mod;
	const fmed_adev *adev = NULL;
	uint i, ndev;
	char buf[64];
	size_t n;

	ffui_view_clear(&gg->wdev.vdev);

	uint mode = (cmd == DEVLIST_SHOW) ? FMED_MOD_INFO_ADEV_OUT : FMED_MOD_INFO_ADEV_IN;
	if (NULL == (mod = core->getmod2(mode, NULL, 0)))
		goto end;
	if (NULL == (adev = mod->m->iface("adev")))
		goto end;

	mode = (cmd == DEVLIST_SHOW) ? FMED_ADEV_PLAYBACK : FMED_ADEV_CAPTURE;
	ndev = adev->list(&ents, mode);
	if ((int)ndev < 0)
		goto end;

	uint sel_dev_index = (cmd == DEVLIST_SHOW) ? core->props->playback_dev_index : 0;

	ffui_view_settextz(&it, "0");
	if (sel_dev_index == 0)
		ffui_view_select(&it, 1);
	ffui_view_append(&gg->wdev.vdev, &it);
	ffui_view_settextz(&it, "(default)");
	ffui_view_set(&gg->wdev.vdev, VDEV_NAME, &it);

	for (i = 0;  i != ndev;  i++) {
		n = ffs_fromint(i + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		if (sel_dev_index == i + 1)
			ffui_view_select(&it, 1);
		ffui_view_append(&gg->wdev.vdev, &it);

		ffui_view_settextz(&it, ents[i].name);
		ffui_view_set(&gg->wdev.vdev, VDEV_NAME, &it);
	}

end:
	if (ents != NULL)
		adev->listfree(ents);

	char *title = ffsz_alfmt("Choose an Audio Device (%s)"
		, (cmd == DEVLIST_SHOW) ? "Playback" : "Capture");
	if (title != NULL)
		ffui_settextz(&gg->wdev.wnd, title);
	ffmem_free(title);

	ffui_show(&gg->wdev.wnd, 1);
	ffui_wnd_setfront(&gg->wdev.wnd);
	gg->devlist_rec = (cmd != DEVLIST_SHOW);
}

static void devlist_sel()
{
	uint idev = ffui_view_selnext(&gg->wdev.vdev, -1);
	if ((int)idev < 0)
		return;

	if (gg->devlist_rec) {
		rec_setdev(idev);

	} else {
		core->props->playback_dev_index = idev;
	}

	ffui_show(&gg->wdev.wnd, 0);
}

static void gui_dev_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case DEVLIST_SELOK:
		devlist_sel();
		break;
	}
}


void winfo_init()
{
	gg->winfo.winfo.hide_on_close = 1;
	gg->winfo.winfo.on_action = &gui_info_action;
	ffui_view_showgroups(&gg->winfo.vinfo, 1);
}

static void gui_info_click(void)
{
	int i, isub;
	ffui_point pt;
	ffui_cur_pos(&pt);
	if (-1 == (i = ffui_view_hittest(&gg->winfo.vinfo, &pt, &isub))
		|| isub != VINFO_VAL)
		return;
	ffui_view_edit(&gg->winfo.vinfo, i, VINFO_VAL);
}

static void gui_info_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case INFOEDIT:
		gui_info_click();
		break;
	}
}

static void winfo_addpair(const ffstr *name, const ffstr *val)
{
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffui_view_settextstr(&it, name);
	ffui_view_setgroupid(&it, 0);
	ffui_view_append(&gg->winfo.vinfo, &it);

	ffui_view_settextstr(&it, val);
	ffui_view_set(&gg->winfo.vinfo, 1, &it);
}

void gui_media_showinfo(void)
{
	fmed_que_entry *e;
	ffui_viewgrp vg;
	int i, grp = 0;
	ffstr name, empty = {}, *val;
	ffarr data = {};

	ffui_show(&gg->winfo.winfo, 1);

	if (-1 == (i = ffui_view_selnext(&gg->wmain.vlist, -1))) {
		ffui_view_clear(&gg->winfo.vinfo);
		ffui_view_cleargroups(&gg->winfo.vinfo);
		return;
	}

	e = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);

	ffui_settextstr(&gg->winfo.winfo, &e->url);

	ffui_redraw(&gg->winfo.vinfo, 0);
	ffui_view_clear(&gg->winfo.vinfo);
	ffui_view_cleargroups(&gg->winfo.vinfo);

	ffui_viewgrp_reset(&vg);
	ffui_viewgrp_settextz(&vg, "Metadata");
	ffui_view_insgrp(&gg->winfo.vinfo, -1, grp, &vg);

	ffstr_setz(&name, "File path");
	winfo_addpair(&name, &e->url);

	fffileinfo fi = {};
	ffbool have_fi = (0 == fffile_infofn(e->url.ptr, &fi));
	ffarr_alloc(&data, 255);

	ffstr_setz(&name, "File size");
	data.len = 0;
	if (have_fi)
		ffstr_catfmt(&data, "%U KB", fffile_infosize(&fi) / 1024);
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "File date");
	data.len = 0;
	if (have_fi) {
		ffdtm dt;
		fftime t = fffile_infomtime(&fi);
		fftime_split(&dt, &t, FFTIME_TZLOCAL);
		data.len = fftime_tostr(&dt, data.ptr, data.cap, FFTIME_DATE_WDMY | FFTIME_HMS);
	}
	winfo_addpair(&name, (ffstr*)&data);

	ffstr_setz(&name, "Info");
	if (NULL == (val = gg->qu->meta_find(e, FFSTR("__info"))))
		val = &empty;
	winfo_addpair(&name, val);

	for (i = 0;  NULL != (val = gg->qu->meta(e, i, &name, 0));  i++) {
		if (val == FMED_QUE_SKIP)
			continue;
		winfo_addpair(&name, val);
	}
	ffui_redraw(&gg->winfo.vinfo, 1);
	ffarr_free(&data);
}


void wgoto_init()
{
	gg->wgoto.wgoto.hide_on_close = 1;
	gg->wgoto.wgoto.on_action = &gui_wgoto_action;
}

static void gui_wgoto_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case A_PLAY_GOTO: {
		ffstr s;
		ffdtm dt;
		fftime t;

		ffui_textstr(&gg->wgoto.etime, &s);
		if (s.len != fftime_fromstr(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
			return;

		fftime_join(&t, &dt, FFTIME_TZNODATE);
		ffui_trk_set(&gg->wmain.tpos, fftime_sec(&t));
		gui_seek(A_PLAY_GOTO);
		ffui_show(&gg->wgoto.wgoto, 0);
		break;
	}
	}
}


void wabout_init(void)
{
	char buf[255];
	int r = ffs_fmt(buf, buf + sizeof(buf),
		"fmedia v%s\n\n"
		"Fast media player, recorder, converter"
		, core->props->version_str);
	ffui_settext(&gg->wabout.labout, buf, r);
	ffui_settextz(&gg->wabout.lurl, FMED_HOMEPAGE);
	gg->wabout.wabout.hide_on_close = 1;
	gg->wabout.wabout.on_action = &gui_wabout_action;
}

static void gui_wabout_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case OPEN_HOMEPAGE:
		if (0 != ffui_shellexec(FMED_HOMEPAGE, SW_SHOWNORMAL))
			syserrlog(core, NULL, "gui", "ShellExecute()");
		break;
	}
}


void wuri_init(void)
{
	gg->wuri.wuri.hide_on_close = 1;
	gg->wuri.wuri.on_action = &gui_wuri_action;
}

static void cmd_url_add()
{
	ffstr s;
	ffui_textstr(&gg->wuri.turi, &s);
	if (s.len != 0)
		gui_media_add2(s.ptr, -1, 0);
	ffstr_free(&s);
	ffui_show(&gg->wuri.wuri, 0);
}

static const struct cmd wuri_cmds[] = {
	{ URL_ADD,	F0 | CMD_FCORE,	&cmd_url_add },
};

static void gui_wuri_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, wuri_cmds, FFCNT(wuri_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	switch (id) {
	case URL_CLOSE:
		ffui_show(&gg->wuri.wuri, 0);
		break;
	}
}


void wfilter_init(void)
{
	gg->wfilter.wnd.hide_on_close = 1;
	gg->wfilter.wnd.on_action = &gui_wfilter_action;
}

static void gui_filt_apply()
{
	ffstr s;
	ffui_textstr(&gg->wfilter.ttext, &s);
	uint flags = GUI_FILT_META;
	if (ffui_chbox_checked(&gg->wfilter.cbfilename))
		flags |= GUI_FILT_URL;
	gui_filter(&s, flags);
	ffstr_free(&s);
}

static const struct cmd wfilt_cmds[] = {
	{ FILTER_APPLY,	F0 | CMD_FCORE,	&gui_filt_apply },
};

static void gui_wfilter_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, wfilt_cmds, FFCNT(wfilt_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	switch (id) {
	case FILTER_RESET:
		ffui_cleartext(&gg->wfilter.ttext);
		break;
	}
}


const char* const repeat_str[3] = { "None", "Track", "Playlist" };
