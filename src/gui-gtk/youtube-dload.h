/** fmedia: GUI for downloading content from Internet
2020, Simon Zolin */

#include <FFOS/process.h>

struct subps;
struct gui_wdload {
	ffui_wnd wdload;
	ffui_label lurl;
	ffui_edit eurl;
	ffui_btn bshowfmt;
	ffui_text tlog;

	ffui_label lcmdline;
	ffui_edit ecmdline;
	ffui_label lout;
	ffui_edit eout;
	ffui_btn bdl;

	ffbyte wconf_flags[2];
	struct subps *subps;

	int first :1;
};

const ffui_ldr_ctl wdload_ctls[] = {
	FFUI_LDR_CTL(struct gui_wdload, wdload),
	FFUI_LDR_CTL(struct gui_wdload, lurl),
	FFUI_LDR_CTL(struct gui_wdload, eurl),
	FFUI_LDR_CTL(struct gui_wdload, bshowfmt),
	FFUI_LDR_CTL(struct gui_wdload, tlog),
	FFUI_LDR_CTL(struct gui_wdload, lcmdline),
	FFUI_LDR_CTL(struct gui_wdload, ecmdline),
	FFUI_LDR_CTL(struct gui_wdload, lout),
	FFUI_LDR_CTL(struct gui_wdload, eout),
	FFUI_LDR_CTL(struct gui_wdload, bdl),
	FFUI_LDR_CTL_END
};

#define YDL  "/usr/bin/youtube-dl"

/** Subprocess context */
struct subps {
	fftask task;
	ffstr url;
	ffstr formats;
	char *workdir;
	ffps ps;
	fffd rd, wr;
	ffvec data;
	ffkevent kqtask;
	int active;
};

static void subps_destroy(struct subps *sp)
{
	dbglog("subps_destroy");
	if (sp == NULL || !sp->active)
		return;
	sp->active = 0;

	if (sp->ps != FFPS_NULL) {
		ffps_kill(sp->ps);
		dbglog("killed child process");
		ffps_close(sp->ps);  sp->ps = FFPS_NULL;
	}
	ffpipe_close(sp->rd);  sp->rd = FFPIPE_NULL;
	ffpipe_close(sp->wr);  sp->wr = FFPIPE_NULL;
	ffvec_free(&sp->data);
	ffstr_free(&sp->url);
	ffstr_free(&sp->formats);
	ffmem_free(sp->workdir); sp->workdir = NULL;
}

static void subps_progress(struct subps *sp);
static void wdload_status_add_enqueue(int id, const ffstr *s);

static int pipe_prepare(struct subps *sp)
{
	if (0 != ffpipe_create(&sp->rd, &sp->wr))
		return -1;
	if (0 != ffpipe_nonblock(sp->rd, 1))
		return -1;
	ffkev_init(&sp->kqtask);
	sp->kqtask.oneshot = 0;
	sp->kqtask.fd = sp->rd;
	sp->kqtask.handler = (ffkev_handler)subps_progress;
	sp->kqtask.udata = sp;
	if (0 != ffkev_attach(&sp->kqtask, core->kq, FFKQU_READ))
		return -1;
	return 0;
}

static void subps_status(struct subps *sp)
{
	wdload_status_add_enqueue(0, (ffstr*)&sp->data);
	sp->data.len = 0;
}

/** Execute subprocess and start reading its output */
static void subps_exec(struct subps *sp, char **args)
{
	int rc = -1;

	sp->ps = FFPS_NULL;
	sp->rd = FFPIPE_NULL;
	sp->wr = FFPIPE_NULL;
	sp->active = 1;

	if (0 != pipe_prepare(sp))
		goto end;

	ffps_execinfo info = {};
	info.workdir = sp->workdir;
	info.in = -1;
	info.out = sp->wr;
	info.err = sp->wr;
	info.argv = (const char**)args;
	info.env = (const char**)environ;

	sp->ps = ffps_exec_info(YDL, &info);
	dbglog("ffps_exec_info: %d", sp->ps);
	if (sp->ps == FFPS_NULL) {
		ffvec_addfmt(&sp->data, "[ps exec: %s: %E]\n"
			, YDL, fferr_last());
		goto end;
	} else {
		ffvec_addfmt(&sp->data, "[ps exec: %s PID %d]\n"
			, YDL, ffps_id(sp->ps));
	}

	rc = 0;

end:
	subps_status(sp);
	if (rc != 0) {
		subps_destroy(sp);
		return;
	}

	subps_progress(sp);
}

/** Read subprocess' output and pass the data to GUI */
static void subps_progress(struct subps *sp)
{
	int flags = 0;
	for (;;) {
		char buf[4096];
		int r = ffpipe_read(sp->rd, buf, sizeof(buf));
		dbglog("ffpipe_read: %d", r);
		if (r > 0) {
			ffvec_addT(&sp->data, buf, r, char);
			flags |= 1;
		} else if (r < 0) {
			if (fferr_again(fferr_last())) {
				break;
			}
			syserrlog("%s", "ffpipe_read");
			ffvec_addfmt(&sp->data, "[ffpipe_read: %E]\n", fferr_last());
			flags |= 2;
			break;
		} else if (r == 0) {
			flags |= 2;
			break;
		}
	}

	if (sp->ps == FFPS_NULL)
		flags |= 2;

	if (flags != 0)
		subps_status(sp);

	if (flags & 2)
		subps_destroy(sp);
}

/** Show available formats */
static void subps_list_fmts(struct subps *sp)
{
	char *args[4];
	args[0] = "youtube-dl";
	args[1] = "--list-formats";
	args[2] = ffsz_dupn(sp->url.ptr, sp->url.len);
	args[3] = NULL;
	subps_exec(sp, args);
	ffmem_free(args[2]);
}

/** Download content */
static void subps_dload_url(struct subps *sp)
{
	char *args[5];
	args[0] = "youtube-dl";
	args[1] = "-f";
	args[2] = ffsz_dupn(sp->formats.ptr, sp->formats.len);
	args[3] = ffsz_dupn(sp->url.ptr, sp->url.len);
	args[4] = NULL;
	subps_exec(sp, args);
	ffmem_free(args[2]);
	ffmem_free(args[3]);
}

void wdload_subps_onsig(struct ffsig_info *info, int exit_code)
{
	struct gui_wdload *w = gg->wdload;
	struct subps *sp = w->subps;
	if (sp == NULL || info->pid != sp->ps)
		return;

	sp->ps = FFPS_NULL;
	dbglog("ffps_wait: %d", exit_code);
	ffvec_addfmt(&sp->data, "\n[Program exited with code %u]\n", exit_code);
	subps_progress(sp);
}

/** Thread: GUI */
static void wdload_status_add(void *param)
{
	ffstr *s = param;
	ffui_text_addtextstr(&gg->wdload->tlog, s);
	ffui_text_scroll(&gg->wdload->tlog, -1);
	ffstr_free(s);
	ffmem_free(s);
}

static void wdload_status_add_enqueue(int id, const ffstr *s)
{
	ffstr *sc = ffmem_new(ffstr);
	ffstr_dup2(sc, s);
	ffui_thd_post(wdload_status_add, sc, 0);
}

struct subps* subps_new()
{
	struct gui_wdload *w = gg->wdload;
	if (w->subps == NULL) {
		w->subps = ffmem_new(struct subps);
	} else {
		subps_destroy(w->subps);
		ffmem_zero_obj(w->subps);
	}
	return w->subps;
}

void wdload_destroy()
{
	struct gui_wdload *w = gg->wdload;
	subps_destroy(w->subps);
	ffmem_free(w->subps);
	ffmem_free0(gg->wdload);
}

static void wdload_action(ffui_wnd *wnd, int id)
{
	struct gui_wdload *w = gg->wdload;
	dbglog("%s cmd:%u", __func__, id);

	switch (id) {
	case A_DLOAD_SHOWFMT: {
		ffstr url = {};
		ffui_edit_textstr(&w->eurl, &url);
		ffui_text_clear(&w->tlog);
		if (url.len == 0) {
			ffui_text_addtextz(&w->tlog, "URL value must not be empty");
		} else {
			struct subps *sp = subps_new();
			sp->url = url;
			fftask_set(&sp->task, (fftask_handler)subps_list_fmts, sp);
			core->task(&sp->task, FMED_TASK_POST);
			break;
		}
		ffstr_free(&url);
		break;
	}

	case A_DLOAD_DL: {
		ffstr url = {}, formats = {}, out = {};
		ffui_edit_textstr(&w->eurl, &url);
		ffui_edit_textstr(&w->ecmdline, &formats);
		ffui_edit_textstr(&w->eout, &out);
		ffui_text_clear(&w->tlog);
		if (url.len == 0 || formats.len == 0 || out.len == 0) {
			ffui_text_addtextz(&w->tlog, "URL, Formats and OutDir values must not be empty");
		} else {
			gg->conf.ydl_format = ffsz_dup(formats.ptr);
			gg->conf.ydl_outdir = ffsz_dup(out.ptr);

			struct subps *sp = subps_new();
			sp->url = url;
			sp->formats = formats;
			sp->workdir = out.ptr;
			fftask_set(&sp->task, (fftask_handler)subps_dload_url, sp);
			core->task(&sp->task, FMED_TASK_POST);
			break;
		}
		ffstr_free(&url);
		ffstr_free(&formats);
		ffstr_free(&out);
		break;
	}
	}
}

static void wdload_writeval(ffconfw *conf, ffuint i)
{
	ffstr s = {};

	switch (i) {
	case 0:
		ffstr_setz(&s, gg->conf.ydl_format);
		ffconfw_addstr(conf, &s);
		break;
	case 1:
		ffstr_setz(&s, gg->conf.ydl_outdir);
		ffconfw_addstr(conf, &s);
		break;
	}
}

int wdload_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wdload *w = gg->wdload;
	static const char setts[][19] = {
		"gui.gui.ydl_format",
		"gui.gui.ydl_outdir",
	};

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
			if (!w->wconf_flags[i]) {
				ffconfw_addkeyz(conf, setts[i]);
				wdload_writeval(conf, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
		if (ffstr_matchz(line, setts[i])) {
			ffconfw_addkeyz(conf, setts[i]);
			wdload_writeval(conf, i);
			w->wconf_flags[i] = 1;
			return 1;
		}
	}
	return 0;
}

void wdload_show(uint show)
{
	struct gui_wdload *w = gg->wdload;

	if (!show) {
		ffui_show(&w->wdload, 0);
		return;
	}

	if (w->first) {
		w->first = 0;
		ffui_text_setmonospace(&w->tlog, 1);
		const char *s = gg->conf.ydl_format;
		if (s != NULL && s[0] != '\0')
			ffui_edit_settextz(&w->ecmdline, s);

		s = gg->conf.ydl_outdir;
		if (s == NULL || s[0] == '\0')
			s = gg->home_dir;
		ffui_edit_settextz(&w->eout, s);
	}
	ffui_show(&w->wdload, 1);
}

void wdload_init()
{
	struct gui_wdload *w = ffmem_new(struct gui_wdload);
	gg->wdload = w;
	w->wdload.hide_on_close = 1;
	w->wdload.on_action = &wdload_action;
	gg->home_dir = core->env_expand(NULL, 0, "$HOME");
	w->first = 1;
}
