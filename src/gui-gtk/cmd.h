/** fmedia: gui-gtk: quick command
2021, Simon Zolin */


struct gui_wcmd {
	ffui_wnd wnd;
	ffui_edit tfilter;
	ffui_view vlist;

	ffstr filter;
};

const ffui_ldr_ctl wcmd_ctls[] = {
	FFUI_LDR_CTL(struct gui_wcmd, wnd),
	FFUI_LDR_CTL(struct gui_wcmd, tfilter),
	FFUI_LDR_CTL(struct gui_wcmd, vlist),
	FFUI_LDR_CTL_END
};

struct cmdent {
	const char *name;
	ffuint id;
};

const struct cmdent action_str[] = {
	{ "About",	A_ABOUT },
	{ "Add URL...",	A_LIST_ADDURL },
	{ "Add...",	A_LIST_ADDFILE },
	{ "Analyze & Show PCM Info",	A_FILE_SHOWPCM },
	{ "Clear",	A_LIST_CLEAR },
	{ "Close Tab",	A_LIST_DEL },
	{ "Convert...",	A_SHOWCONVERT },
	{ "Rename File...",	A_SHOW_RENAME },
	{ "Delete From Disk",	A_FILE_DELFILE },
	{ "Download from YouTube...",	A_DLOAD_SHOW },
	{ "Edit Default Settings",	A_CONF_EDIT },
	{ "Edit GUI",	A_FMEDGUI_EDIT },
	{ "Edit User Settings...",	A_USRCONF_EDIT },
	{ "Exit",	A_QUIT },
	{ "Jump To Marker",	A_GOPOS },
	{ "Leap Back",	A_LEAP_BACK },
	{ "Leap Forward",	A_LEAP_FWD },
	{ "Minimize to Tray",	A_HIDE },
	{ "New Tab",	A_LIST_NEW },
	{ "Next",	A_NEXT },
	{ "Play/Pause",	A_PLAYPAUSE },
	{ "Previous",	A_PREV },
	{ "Properties...",	A_SHOW_PROPS },
	{ "Random",	A_LIST_RANDOM },
	{ "Read Meta Tags",	A_LIST_READMETA },
	{ "Remove Dead Items",	A_LIST_RMDEAD },
	{ "Remove",	A_LIST_REMOVE },
	{ "Repeat: None/Track/All",	A_PLAY_REPEAT },
	{ "Reset Volume",	A_VOLRESET },
	{ "Save Playlist...",	A_LIST_SAVE },
	{ "Seek Back",	A_RWND },
	{ "Seek Forward",	A_FFWD },
	{ "Select All",	A_LIST_SELECTALL },
	{ "Set Marker",	A_SETGOPOS },
	{ "Set Seek Position",	A_CONV_SET_SEEK },
	{ "Set Until Position",	A_CONV_SET_UNTIL },
	{ "Show Changelog",	A_CHANGES_SHOW },
	{ "Show File in Explorer",	A_FILE_SHOWDIR },
	{ "Show Media Info",	A_FILE_SHOWINFO },
	{ "Show Readme File",	A_README_SHOW },
	{ "Sort: Random",	A_LIST_SORTRANDOM },
	{ "Stop After Current",	A_STOP_AFTER },
	{ "Stop",	A_STOP },
	{ "Volume Down",	A_VOLDOWN },
	{ "Volume Up",	A_VOLUP },
};

void wcmd_disp()
{
	struct gui_wcmd *w = gg->wcmd;
	struct ffui_view_disp *disp = &w->vlist.disp;

	ffstr val;
	val.len = -1;
	char *zval = NULL;

	switch (disp->sub) {
	case 0:
		ffstr_setz(&val, action_str[disp->idx].name);
		break;
	}

	if (zval != NULL)
		ffstr_setz(&val, zval);

	if (val.len != (ffsize)-1)
		disp->text.len = ffs_append(disp->text.ptr, 0, disp->text.len, val.ptr, val.len);

	ffmem_free(zval);
}

void wcmd_action(ffui_wnd *wnd, int id)
{
	struct gui_wcmd *w = gg->wcmd;
	switch (id) {

	case A_CMD_FILTER:
		break;

	case A_CMD_DISP:
		wcmd_disp();
		break;

	case A_CMD_EXEC: {
		int i;
		if (-1 == (i = ffui_view_focused(&w->vlist)))
			break;
		wmain_cmd(action_str[i].id);
		break;
	}
	}
}

void wcmd_show(uint show)
{
	struct gui_wcmd *w = gg->wcmd;

	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}

	ffui_view_setdata(&w->vlist, 0, FF_COUNT(action_str));
	ffui_show(&w->wnd, 1);
	ffui_wnd_present(&w->wnd);
}

void wcmd_init()
{
	struct gui_wcmd *w = ffmem_new(struct gui_wcmd);
	gg->wcmd = w;
	w->wnd.hide_on_close = 1;
	w->wnd.on_action = wcmd_action;
	w->vlist.dispinfo_id = A_CMD_DISP;
}
