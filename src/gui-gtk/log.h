/** fmedia: gui-gtk: log window
2021, Simon Zolin */

struct gui_wlog {
	ffui_wnd wnd;
	ffui_text tlog;
};

const ffui_ldr_ctl wlog_ctls[] = {
	FFUI_LDR_CTL(struct gui_wlog, wnd),
	FFUI_LDR_CTL(struct gui_wlog, tlog),
	FFUI_LDR_CTL_END
};

static void wlog_posted(void *param);

// TIME :TID LEVEL MOD: *ID: ["IN_FILENAME": ] TEXT
static void gui_log(uint flags, fmed_logdata *ld)
{
	ffvec *str = ffmem_new(ffvec);
	ffvec_addfmt(str, "%s :%xU [%s] %s: "
		, ld->stime, ld->tid, ld->level, ld->module);

	if (ld->ctx != NULL)
		ffvec_addfmt(str, "%S:\t", ld->ctx);

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER
		&& ld->trk != NULL) {
		const char *infn = gg->track->getvalstr(ld->trk, "input");
		if (infn != FMED_PNULL)
			ffvec_addfmt(str, "\"%s\": ", infn);
	}

	ffvec_addfmtv(str, ld->fmt, ld->va);

	if (flags & FMED_LOG_SYS)
		ffvec_addfmt(str, ": %E", fferr_last());

	ffvec_addchar(str, '\n');

	ffui_thd_post(wlog_posted, str, 0);
}

static const fmed_log gui_logger = {
	&gui_log
};

/** Thread: GUI */
static void wlog_posted(void *param)
{
	ffvec *str = param;
	ffui_text_addtextstr(&gg->wlog->tlog, str);
	ffvec_free(str);
	ffmem_free(str);
	ffui_text_scroll(&gg->wlog->tlog, -1);
	ffui_show(&gg->wlog->wnd, 1);
}

/** Thread: main */
void wlog_run()
{
	// write log to GUI unless --debug switch is specified
	if ((core->loglev&_FMED_LOG_LEVMASK) != FMED_LOG_DEBUG)
		core->cmd(FMED_SETLOG, &gui_logger);
}

void wlog_show(uint show)
{
	struct gui_wlog *w = gg->wlog;
	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}
}

void wlog_init()
{
	struct gui_wlog *c = ffmem_new(struct gui_wlog);
	c->wnd.hide_on_close = 1;
	gg->wlog = c;
}

void wlog_destroy()
{
	ffmem_free0(gg->wlog);
}
