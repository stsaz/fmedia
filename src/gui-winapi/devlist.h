/** fmedia: gui-winapi: device list
2021, Simon Zolin */

struct gui_wdevlist {
	ffui_wnd wnd;
	ffui_view vdev;
	ffui_btn bok;
};

const ffui_ldr_ctl wdevlist_ctls[] = {
	FFUI_LDR_CTL(struct gui_wdevlist, wnd),
	FFUI_LDR_CTL(struct gui_wdevlist, vdev),
	FFUI_LDR_CTL(struct gui_wdevlist, bok),
	FFUI_LDR_CTL_END
};

enum {
	VDEV_ID,
	VDEV_NAME,
};

void wdev_show(uint show, uint cmd)
{
	struct gui_wdevlist *w = gg->wdev;

	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}

	ffui_viewitem it = {0};
	fmed_adev_ent *ents = NULL;
	const fmed_modinfo *mod;
	const fmed_adev *adev = NULL;
	uint i, ndev;
	char buf[64];
	size_t n;

	ffui_view_clear(&w->vdev);

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
	ffui_view_append(&w->vdev, &it);
	ffui_view_settextz(&it, "(default)");
	ffui_view_set(&w->vdev, VDEV_NAME, &it);

	for (i = 0;  i != ndev;  i++) {
		n = ffs_fromint(i + 1, buf, sizeof(buf), 0);
		ffui_view_settext(&it, buf, n);
		if (sel_dev_index == i + 1)
			ffui_view_select(&it, 1);
		ffui_view_append(&w->vdev, &it);

		ffui_view_settextz(&it, ents[i].name);
		ffui_view_set(&w->vdev, VDEV_NAME, &it);
	}

end:
	if (ents != NULL)
		adev->listfree(ents);

	char *title = ffsz_alfmt("Choose an Audio Device (%s)"
		, (cmd == DEVLIST_SHOW) ? "Playback" : "Capture");
	if (title != NULL)
		ffui_settextz(&w->wnd, title);
	ffmem_free(title);

	ffui_show(&w->wnd, 1);
	ffui_wnd_setfront(&w->wnd);
	gg->devlist_rec = (cmd != DEVLIST_SHOW);
}

void devlist_sel()
{
	struct gui_wdevlist *w = gg->wdev;
	uint idev = ffui_view_selnext(&w->vdev, -1);
	if ((int)idev < 0)
		return;

	if (gg->devlist_rec) {
		rec_setdev(idev);

	} else {
		core->props->playback_dev_index = idev;
	}

	ffui_show(&w->wnd, 0);
}

void wdev_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case DEVLIST_SELOK:
		devlist_sel();
		break;
	}
}

void wdev_init()
{
	struct gui_wdevlist *w = ffmem_new(struct gui_wdevlist);
	gg->wdev = w;
	w->wnd.hide_on_close = 1;
	w->wnd.on_action = wdev_action;
}
