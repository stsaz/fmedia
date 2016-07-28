/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>
#include <FF/time.h>


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

static void gui_wgoto_action(ffui_wnd *wnd, int id);

static void gui_wuri_action(ffui_wnd *wnd, int id);


void wconvert_init()
{
	gg->wconvert.wconvert.hide_on_close = 1;
	gg->wconvert.wconvert.on_action = &gui_cvt_action;
	gg->wconvert.vsets.edit_id = CVT_SETS_EDITDONE;
}

static const struct cmd cvt_cmds[] = {
	{ OUTBROWSE,	F0,	&gui_conv_browse },
	{ CONVERT,	F0 | CMD_FCORE,	&gui_convert },
};

static void gui_cvt_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, cvt_cmds, FFCNT(cvt_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
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

enum CVTF {
	CVTF_EMPTY = 0x010000, //allow empty value
	CVTF_FLT = 0x020000,
	CVTF_STR = 0x040000,
	CVTF_MSEC = 0x080000,
	CVTF_FLT10 = 0x100000,
	CVTF_FLT100 = 0x200000,
};

struct cvt_set {
	const char *settname;
	const char *name;
	const char *desc;
	uint flags; //enum CVTF
};

// gui -> core
static const struct cvt_set cvt_sets[] = {
	{ "ogg-quality", "OGG Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 | FFOFF(cvt_sets_t, ogg_quality) },
	{ "mpeg-quality", "MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", FFOFF(cvt_sets_t, mpg_quality) },
	{ "aac-quality", "AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", FFOFF(cvt_sets_t, aac_quality) },
	{ "flac_complevel", "FLAC Compression", "0..8", FFOFF(cvt_sets_t, flac_complevel) },

	{ "conv_pcm_rate", "Sample rate (Hz)", "", CVTF_EMPTY | FFOFF(cvt_sets_t, conv_pcm_rate) },
	{ "conv_channels", "Mono", "mix | left | right", CVTF_EMPTY | FFOFF(cvt_sets_t, conv_channels) },
	{ "gain", "Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_EMPTY | FFOFF(cvt_sets_t, gain) },
	{ "meta", "Meta Tags", "[clear;]NAME=VAL;...", CVTF_STR | CVTF_EMPTY | FFOFF(cvt_sets_t, meta) },
	{ "seek_time", "Seek to", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(cvt_sets_t, seek) },
	{ "until_time", "Stop at", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(cvt_sets_t, until) },

	{ "overwrite", "Overwrite Output File", "0 or 1", FFOFF(cvt_sets_t, overwrite) },
	{ "out_preserve_date", "Preserve Date", "0 or 1", FFOFF(cvt_sets_t, out_preserve_date) },
};

static const char *const cvt_channels_str[] = { "mix", "left", "right" };

// conf -> gui
static const ffpars_arg cvt_sets_conf[] = {
	{ "ogg_quality",	FFPARS_TFLOAT, FFPARS_DSTOFF(cvt_sets_t, ogg_quality_f) },
	{ "mpeg_quality",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, mpg_quality) },
	{ "aac_quality",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, aac_quality) },
	{ "flac_complevel",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, flac_complevel) },

	{ "pcm_rate",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, conv_pcm_rate) },
	{ "gain",	FFPARS_TFLOAT, FFPARS_DSTOFF(cvt_sets_t, gain_f) },

	{ "overwrite",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, overwrite) },
	{ "preserve_date",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, out_preserve_date) },
};

static void gui_cvt_sets_init(cvt_sets_t *sets)
{
	if (sets->init)
		return;
	sets->init = 1;

	sets->ogg_quality_f = 5.0;
	sets->mpg_quality = 2;
	sets->aac_quality = 192;
	sets->flac_complevel = 6;
	sets->out_preserve_date = 1;
}

int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	gui_cvt_sets_init(&gg->conv_sets);
	ffpars_setargs(ctx, &gg->conv_sets, cvt_sets_conf, FFCNT(cvt_sets_conf));
	return 0;
}

static int sett_tostr(const struct cvt_set *sett, ffarr *dst)
{
	void *src = (char*)&gg->conv_sets + (sett->flags & 0xffff);
	if (sett->flags & CVTF_STR) {
		if (src != NULL)
			ffstr_catfmt(dst, "%s", (char*)src);

	} else if (sett->flags & CVTF_FLT) {
		float f = *(float*)src;

		if ((sett->flags & CVTF_EMPTY) && f == 0)
			return 0;

		if (NULL == ffarr_realloc(dst, FFINT_MAXCHARS + FFSLEN(".00")))
			return 0;
		if (sett->flags & CVTF_FLT100)
			ffstr_catfmt(dst, "%.2F", (double)f);
		else if (sett->flags & CVTF_FLT10)
			ffstr_catfmt(dst, "%.1F", (double)f);

	} else {
		int i = *(int*)src;

		if ((sett->flags & CVTF_EMPTY) && i == 0)
			return 0;

		ffstr_catfmt(dst, "%d", i);
	}
	return 0;
}

static void cvt_sets_destroy(cvt_sets_t *sets)
{
	ffmem_safefree(sets->meta);
}

void gui_showconvert(void)
{
	if (0 == ffui_view_selcount(&gg->wmain.vlist))
		return;

	if (!gg->wconv_init) {
		gui_cvt_sets_init(&gg->conv_sets);

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffarr s = {0};

		uint i;
		for (i = 0;  i != FFCNT(cvt_sets);  i++) {
			ffui_view_settextz(&it, cvt_sets[i].name);
			ffui_view_append(&gg->wconvert.vsets, &it);

			s.len = 0;
			sett_tostr(&cvt_sets[i], &s);
			ffui_view_settextstr(&it, &s);
			ffui_view_set(&gg->wconvert.vsets, VSETS_VAL, &it);

			ffui_view_settextz(&it, cvt_sets[i].desc);
			ffui_view_set(&gg->wconvert.vsets, VSETS_DESC, &it);
		}

		ffarr_free(&s);
		gg->wconv_init = 1;
	}

	ffui_show(&gg->wconvert.wconvert, 1);
	ffui_wnd_setfront(&gg->wconvert.wconvert);
}

void gui_setconvpos(uint cmd)
{
	char buf[64];
	int pos = ffui_trk_val(&gg->wmain.tpos);
	int r = ffs_fmt2(buf, sizeof(buf), "%02u:%02u", pos / 60, pos % 60);
	if (r <= 0)
		return;

	ffui_viewitem it;
	ffui_view_iteminit(&it);

	const char *name = (cmd == SETCONVPOS_SEEK) ? "seek_time" : "until_time";
	uint i;
	for (i = 0;  i != FFCNT(cvt_sets);  i++) {
		if (!ffsz_cmp(cvt_sets[i].settname, name))
			break;
	}

	ffui_view_setindex(&it, i);
	ffui_view_settext(&it, buf, r);
	ffui_view_set(&gg->wconvert.vsets, VSETS_VAL, &it);
	ffui_view_itemreset(&it);
}

static int gui_cvt_getsettings(cvt_sets_t *sets)
{
	uint k;
	int val, rc = -1;
	double d;
	char *txt;
	size_t len;
	const struct cvt_set *st;
	ffstr name;
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	for (k = 0;  k != FFCNT(cvt_sets);  k++) {
		st = &cvt_sets[k];
		ffstr_setz(&name, st->settname);

		ffui_view_setindex(&it, k);
		ffui_view_gettext(&it);
		ffui_view_get(&gg->wconvert.vsets, VSETS_VAL, &it);

		if (NULL == (txt = ffsz_alcopyqz(ffui_view_textq(&it)))) {
			syserrlog(core, NULL, "gui", "%e", FFERR_BUFALOC);
			ffui_view_itemreset(&it);
			return -1;
		}
		len = ffsz_len(txt);

		void *p = (char*)sets + (st->flags & 0xffff);
		if (st->flags & CVTF_STR) {
			char **pstr = p;
			if ((st->flags & CVTF_EMPTY) && len == 0) {
				*pstr = NULL;
				goto next;
			}
			*pstr = txt;
			continue;
		}

		int *pint = p;

		if ((st->flags & CVTF_EMPTY) && len == 0) {
			*pint = -1;
			goto next;
		}

		if (st->flags & CVTF_MSEC) {
			ffdtm dt;
			fftime t;
			if (len != fftime_fromstr(&dt, txt, len, FFTIME_HMS_MSEC_VAR))
				return FFPARS_EBADVAL;

			fftime_join(&t, &dt, FFTIME_TZNODATE);
			val = fftime_ms(&t);

		} else if (st->flags & CVTF_FLT) {
			if (len != ffs_tofloat(txt, len, &d, 0))
				goto end;

			if (st->flags & CVTF_FLT10)
				val = d * 10;
			else if (st->flags & CVTF_FLT100)
				val = d * 100;

		} else if (ffstr_eqcz(&name, "conv_channels")) {
			if (-1 == (val = ffs_ifindarrz(cvt_channels_str, FFCNT(cvt_channels_str), txt, len)))
				goto end;
			val = (val << 4) | 1;

		} else {
			if (len != ffs_toint(txt, len, &val, FFS_INT32))
				goto end;
		}

		*pint = val;

next:
		ffmem_free(txt);
	}

	rc = 0;

end:
	if (rc != 0)
		errlog(core, NULL, "gui", "%s: bad value", cvt_sets[k].name);
	ffui_view_itemreset(&it);
	return rc;
}

/** Create new tracks for selected files.  Pass conversion settings to each track.  Start the first added track. */
static void gui_convert(void)
{
	cvt_sets_t sets = {0};
	int i = -1;
	ffui_viewitem it;
	fmed_que_entry e, *qent, *inp;
	ffstr fn, name;
	void *play = NULL;
	int64 val;
	uint k;

	ffui_view_iteminit(&it);
	ffui_textstr(&gg->wconvert.eout, &fn);
	if (fn.len == 0)
		return;

	if (0 != gui_cvt_getsettings(&sets))
		goto end;

	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_setparam(&it, 0);
		ffui_view_get(&gg->wmain.vlist, 0, &it);
		inp = (void*)ffui_view_param(&it);

		ffmem_tzero(&e);
		e.url = inp->url;
		e.from = inp->from;
		e.to = inp->to;
		if (NULL == (qent = (void*)gg->qu->cmd2(FMED_QUE_ADD | FMED_QUE_NO_ONCHANGE, &e, 0))) {
			continue;
		}

		ffstr sname, *sval;
		size_t n;
		for (n = 0;  NULL != (sval = gg->qu->meta(inp, n, &sname, FMED_QUE_NO_TMETA));  n++) {
			gg->qu->meta_set(qent, sname.ptr, sname.len, sval->ptr, sval->len, 0);
		}

		gui_media_added(qent);

		gg->qu->meta_set(qent, FFSTR("output"), fn.ptr, fn.len, FMED_QUE_TRKDICT);
		if (play == NULL)
			play = qent;

		for (k = 0;  k != FFCNT(cvt_sets);  k++) {

			ffstr_setz(&name, cvt_sets[k].settname);

			void *p = (char*)&sets + (cvt_sets[k].flags & 0xffff);

			if (cvt_sets[k].flags & CVTF_STR) {
				char **pstr = p;
				if (*pstr == NULL)
					continue;
				gg->qu->meta_set(qent, name.ptr, name.len, *pstr, ffsz_len(*pstr), FMED_QUE_TRKDICT);
				continue;
			}

			int *pint = p;
			if (*pint == -1)
				continue;
			val = *pint;
			gg->qu->meta_set(qent, name.ptr, name.len
				, (char*)&val, sizeof(int64), FMED_QUE_TRKDICT | FMED_QUE_NUM);
		}
	}

	if (play != NULL) {
		gg->play_id = play;
		gui_corecmd_op(PLAY, NULL);
	}
end:
	ffui_view_itemreset(&it);
	ffstr_free(&fn);
	cvt_sets_destroy(&sets);
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


void wgoto_init()
{
	gg->wgoto.wgoto.hide_on_close = 1;
	gg->wgoto.wgoto.on_action = &gui_wgoto_action;
}

static void gui_wgoto_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case GOTO: {
		ffstr s;
		ffdtm dt;
		fftime t;

		ffui_textstr(&gg->wgoto.etime, &s);
		if (s.len != fftime_fromstr(&dt, s.ptr, s.len, FFTIME_HMS_MSEC_VAR))
			return;

		fftime_join(&t, &dt, FFTIME_TZNODATE);
		ffui_trk_set(&gg->wmain.tpos, t.s);
		gui_seek(GOTO);
		ffui_show(&gg->wgoto.wgoto, 0);
		break;
	}
	}
}


void wabout_init(void)
{
	ffui_settextz(&gg->wabout.labout, "fmedia v" FMED_VER "\nhttp://fmedia.firmdev.com");
	gg->wabout.wabout.hide_on_close = 1;
}


void wuri_init(void)
{
	gg->wuri.wuri.hide_on_close = 1;
	gg->wuri.wuri.on_action = &gui_wuri_action;
}

static void gui_wuri_action(ffui_wnd *wnd, int id)
{
	ffstr s;
	switch (id) {
	case URL_ADD:
		ffui_textstr(&gg->wuri.turi, &s);
		if (s.len != 0)
			gui_media_add1(s.ptr);
		ffstr_free(&s);
		ffui_show(&gg->wuri.wuri, 0);
		break;

	case URL_CLOSE:
		ffui_show(&gg->wuri.wuri, 0);
		break;
	}
}
