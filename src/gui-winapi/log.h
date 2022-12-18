/** fmedia: gui-winapi: log
2021, Simon Zolin */

typedef struct gui_wlog {
	ffui_wnd wlog;
	ffui_edit tlog;
} gui_wlog;

const ffui_ldr_ctl wlog_ctls[] = {
	FFUI_LDR_CTL(struct gui_wlog, wlog),
	FFUI_LDR_CTL(struct gui_wlog, tlog),
	FFUI_LDR_CTL_END
};

// TIME :TID [LEVEL] MOD: *ID: {"IN_FILENAME": } TEXT
void gui_log(uint flags, fmed_logdata *ld)
{
	struct gui_wlog *w = gg->wlog;
	char buf[4096];
	ffuint cap = FFCNT(buf) - FFSLEN("\r\n");
	ffstr s = FFSTR_INITN(buf, 0);

	if (ld->tid != 0) {
		ffstr_addfmt(&s, cap, "%s :%U [%s] %s: "
			, ld->stime, ld->tid, ld->level, ld->module);
	} else {
		ffstr_addfmt(&s, cap, "%s [%s] %s: "
			, ld->stime, ld->level, ld->module);
	}
	if (ld->ctx != NULL)
		ffstr_addfmt(&s, cap, "%S:\t", ld->ctx);

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER && ld->trk != NULL) {
		const char *infn = gg->track->getvalstr(ld->trk, "input");
		if (infn != FMED_PNULL)
			ffstr_addfmt(&s, cap, "\"%s\": ", infn);
	}

	ffstr_addfmtv(&s, cap, ld->fmt, ld->va);
	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": %E", fferr_last());
	s.ptr[s.len++] = '\r';
	s.ptr[s.len++] = '\n';

	fflk_lock(&gg->lklog);
	ffui_edit_addtext(&w->tlog, s.ptr, s.len);
	fflk_unlock(&gg->lklog);

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER)
		ffui_show(&w->wlog, 1);
}

const fmed_log gui_logger = {
	gui_log
};

void wlog_show(uint show)
{
	struct gui_wlog *w = gg->wlog;

	if (!show) {
		ffui_show(&w->wlog, 0);
		return;
	}

	ffui_show(&w->wlog, 1);
}

void wlog_init()
{
	struct gui_wlog *w = ffmem_new(struct gui_wlog);
	gg->wlog = w;
	w->wlog.hide_on_close = 1;
}
