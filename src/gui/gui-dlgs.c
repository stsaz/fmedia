/**
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>

#include <FF/time.h>
#include <FFOS/process.h>


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

static void gui_rec_action(ffui_wnd *wnd, int id);
static void gui_rec_browse(void);

static void gui_info_action(ffui_wnd *wnd, int id);

static void gui_wgoto_action(ffui_wnd *wnd, int id);

static void gui_wabout_action(ffui_wnd *wnd, int id);

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

static int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst);
static int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist);

// gui -> core
static const struct cvt_set cvt_sets[] = {
	{ "conv_pcm_format", "Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_EMPTY | FFOFF(cvt_sets_t, format) },
	{ "conv_pcm_rate", "Sample rate (Hz)", "", CVTF_EMPTY | FFOFF(cvt_sets_t, conv_pcm_rate) },
	{ "conv_channels", "Channels", "2 (stereo) | 1 (mono) | left | right", CVTF_EMPTY | FFOFF(cvt_sets_t, channels) },

	{ "gain", "Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_EMPTY | FFOFF(cvt_sets_t, gain) },
	{ "seek_time", "Seek to", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(cvt_sets_t, seek) },
	{ "until_time", "Stop at", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(cvt_sets_t, until) },

	{ "vorbis.quality", "Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 | FFOFF(cvt_sets_t, vorbis_quality) },
	{ "opus.bitrate", "Opus Bitrate", "6..510", FFOFF(cvt_sets_t, opus_bitrate) },
	{ "mpeg-quality", "MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", FFOFF(cvt_sets_t, mpg_quality) },
	{ "aac-quality", "AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", FFOFF(cvt_sets_t, aac_quality) },
	{ "flac_complevel", "FLAC Compression", "0..8", FFOFF(cvt_sets_t, flac_complevel) },
	{ "stream_copy", "Stream copy", "Don't re-encode OGG/MP3 data (0 or 1)", FFOFF(cvt_sets_t, stream_copy) },
	{ "meta", "Meta Tags", "[clear;]NAME=VAL;...", CVTF_STR | CVTF_EMPTY | FFOFF(cvt_sets_t, meta) },

	{ "overwrite", "Overwrite Output File", "0 or 1", FFOFF(cvt_sets_t, overwrite) },
	{ "out_preserve_date", "Preserve Date", "0 or 1", FFOFF(cvt_sets_t, out_preserve_date) },
};

// conf -> gui
static const ffpars_arg cvt_sets_conf[] = {
	{ "output",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DSTOFF(cvt_sets_t, output) },

	{ "vorbis_quality",	FFPARS_TFLOAT, FFPARS_DSTOFF(cvt_sets_t, vorbis_quality_f) },
	{ "opus_bitrate",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, opus_bitrate) },
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
	sets->init = 1;

	sets->vorbis_quality_f = 5.0;
	sets->opus_bitrate = 128;
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

static int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst)
{
	void *src = (char*)sets + (sett->flags & 0xffff);
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

void cvt_sets_destroy(cvt_sets_t *sets)
{
	ffmem_safefree0(sets->output);
	ffmem_safefree0(sets->meta);
}

void gui_showconvert(void)
{
	if (!gg->wconv_init) {
		if (!gg->conv_sets.init)
			gui_cvt_sets_init(&gg->conv_sets);

		ffui_settextz(&gg->wconvert.eout, gg->conv_sets.output);

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffarr s = {0};

		uint i;
		for (i = 0;  i != FFCNT(cvt_sets);  i++) {
			ffui_view_settextz(&it, cvt_sets[i].name);
			ffui_view_append(&gg->wconvert.vsets, &it);

			s.len = 0;
			sett_tostr(&gg->conv_sets, &cvt_sets[i], &s);
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

static int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist)
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
	for (k = 0;  k != nsets;  k++) {
		st = &psets[k];
		ffstr_setz(&name, st->settname);

		ffui_view_setindex(&it, k);
		ffui_view_gettext(&it);
		ffui_view_get(vlist, VSETS_VAL, &it);

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

		} else if (ffstr_eqcz(&name, "pcm_format")
			|| ffstr_eqcz(&name, "conv_pcm_format")) {
			if (0 > (val = ffpcm_fmt(txt, len)))
				goto end;

		} else if (ffstr_eqcz(&name, "pcm_channels")
			|| ffstr_eqcz(&name, "conv_channels")) {
			if (0 > (val = ffpcm_channels(txt, len)))
				goto end;

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
		errlog(core, NULL, "gui", "%s: bad value", psets[k].name);
	ffui_view_itemreset(&it);
	return rc;
}

/** Create new tracks for selected files.  Pass conversion settings to each track.  Start the first added track. */
static void gui_convert(void)
{
	int i = -1;
	ffui_viewitem it;
	fmed_que_entry e, *qent, *inp;
	ffstr fn, name;
	int64 val;
	uint k;
	ffarr ar = {0};

	cvt_sets_destroy(&gg->conv_sets);

	ffui_view_iteminit(&it);
	ffui_textstr(&gg->wconvert.eout, &fn);
	gg->conv_sets.output = fn.ptr;
	if (fn.len == 0 || 0 == ffui_view_selcount(&gg->wmain.vlist)) {
		errlog(core, NULL, "gui", "convert: no files selected");
		return;
	}

	if (0 != gui_cvt_getsettings(cvt_sets, FFCNT(cvt_sets), &gg->conv_sets, &gg->wconvert.vsets))
		goto end;

	int itab = gui_newtab(GUI_TAB_CONVERT);
	gg->qu->cmd(FMED_QUE_NEW, NULL);
	gg->qu->cmd(FMED_QUE_SEL, (void*)(size_t)itab);

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

		if (NULL == _ffarr_append(&ar, &qent, 1, sizeof(qent)))
			goto end;

		ffstr sname, *sval;
		size_t n;
		for (n = 0;  NULL != (sval = gg->qu->meta(inp, n, &sname, FMED_QUE_NO_TMETA));  n++) {
			gg->qu->meta_set(qent, sname.ptr, sname.len, sval->ptr, sval->len, 0);
		}

		gg->qu->meta_set(qent, FFSTR("output"), fn.ptr, fn.len, FMED_QUE_TRKDICT);

		for (k = 0;  k != FFCNT(cvt_sets);  k++) {

			ffstr_setz(&name, cvt_sets[k].settname);

			void *p = (char*)&gg->conv_sets + (cvt_sets[k].flags & 0xffff);

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

			if (ffstr_eqcz(&name, "gain"))
				qent->trk->audio.gain = *pint;
			else if (ffstr_eqcz(&name, "seek_time"))
				qent->trk->audio.seek = *pint;
			else if (ffstr_eqcz(&name, "until_time"))
				qent->trk->audio.until = *pint;
			else if (ffstr_eqcz(&name, "out_preserve_date"))
				qent->trk->out_preserve_date = *pint;
			else if (ffstr_eqcz(&name, "overwrite"))
				qent->trk->out_overwrite = *pint;
			else if (ffstr_eqcz(&name, "conv_pcm_format"))
				qent->trk->audio.convfmt.format = *pint;
			else if (ffstr_eqcz(&name, "conv_channels"))
				qent->trk->audio.convfmt.channels = *pint;
			else if (ffstr_eqcz(&name, "conv_pcm_rate"))
				qent->trk->audio.convfmt.sample_rate = *pint;

			val = *pint;
			gg->qu->meta_set(qent, name.ptr, name.len
				, (char*)&val, sizeof(int64), FMED_QUE_TRKDICT | FMED_QUE_NUM);
		}
	}

	ffui_view_clear(&gg->wmain.vlist);
	fmed_que_entry **pq;
	FFARR_WALKT(&ar, pq, fmed_que_entry*) {
		gui_media_added(*pq, 0);
	}

	if (ar.len != 0) {
		qent = *(void**)ar.ptr;
		gui_corecmd_add(&cmd_play, qent);
	}

end:
	ffarr_free(&ar);
	ffui_view_itemreset(&it);
}

static void gui_conv_browse(void)
{
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_OUTPUT);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &gg->wconvert.wconvert, NULL, 0)))
		return;

	ffui_settextz(&gg->wconvert.eout, fn);
}


void wrec_init()
{
	gg->wrec.wrec.hide_on_close = 1;
	gg->wrec.wrec.on_action = &gui_rec_action;
	gg->wrec.vsets.edit_id = CVT_SETS_EDITDONE;
}

// gui -> core
static const struct cvt_set rec_sets[] = {
	{ "pcm_format", "Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_EMPTY | FFOFF(rec_sets_t, format) },
	{ "pcm_sample_rate", "Sample Rate (Hz)", "", CVTF_EMPTY | FFOFF(rec_sets_t, sample_rate) },
	{ "pcm_channels", "Channels", "2 (stereo) | 1 (mono) | left | right", CVTF_EMPTY | FFOFF(rec_sets_t, channels) },
	{ "gain", "Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_EMPTY | FFOFF(rec_sets_t, gain) },
	// { "until_time", "Stop at", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(rec_sets_t, until) },

	{ "vorbis.quality", "Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 | FFOFF(rec_sets_t, vorbis_quality) },
	{ "opus.bitrate", "Opus Bitrate", "6..510", FFOFF(rec_sets_t, opus_bitrate) },
	{ "mpeg-quality", "MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", FFOFF(rec_sets_t, mpg_quality) },
	{ "flac_complevel", "FLAC Compression", "0..8", FFOFF(rec_sets_t, flac_complevel) },
};

// conf -> gui
static const ffpars_arg rec_sets_conf[] = {
	{ "output",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DSTOFF(rec_sets_t, output) },
	{ "gain",	FFPARS_TFLOAT, FFPARS_DSTOFF(rec_sets_t, gain_f) },

	{ "vorbis_quality",	FFPARS_TFLOAT, FFPARS_DSTOFF(rec_sets_t, vorbis_quality_f) },
	{ "opus_bitrate",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, opus_bitrate) },
	{ "mpeg_quality",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, mpg_quality) },
	{ "flac_complevel",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, flac_complevel) },
};

static void rec_sets_init(rec_sets_t *sets)
{
	sets->init = 1;

	sets->vorbis_quality_f = 5.0;
	sets->opus_bitrate = 128;
	sets->mpg_quality = 2;
	sets->flac_complevel = 6;
}

void rec_sets_destroy(rec_sets_t *sets)
{
	ffmem_safefree0(sets->output);
}

int gui_conf_rec(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	rec_sets_init(&gg->rec_sets);
	ffpars_setargs(ctx, &gg->rec_sets, rec_sets_conf, FFCNT(rec_sets_conf));
	return 0;
}

void gui_rec_show(void)
{
	if (!gg->wrec_init) {
		if (!gg->rec_sets.init)
			rec_sets_init(&gg->rec_sets);

		ffui_settextz(&gg->wrec.eout, gg->rec_sets.output);

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffarr s = {0};

		uint i;
		for (i = 0;  i != FFCNT(rec_sets);  i++) {
			ffui_view_settextz(&it, rec_sets[i].name);
			ffui_view_append(&gg->wrec.vsets, &it);

			s.len = 0;
			sett_tostr(&gg->rec_sets, &rec_sets[i], &s);
			ffui_view_settextstr(&it, &s);
			ffui_view_set(&gg->wrec.vsets, VSETS_VAL, &it);

			ffui_view_settextz(&it, rec_sets[i].desc);
			ffui_view_set(&gg->wrec.vsets, VSETS_DESC, &it);
		}

		ffarr_free(&s);
		gg->wrec_init = 1;
	}

	ffui_show(&gg->wrec.wrec, 1);
	ffui_wnd_setfront(&gg->wrec.wrec);
}

static const struct cmd rec_cmds[] = {
	{ REC,	F1 | CMD_FCORE,	&gui_rec },
	{ OUTBROWSE,	F0,	&gui_rec_browse },
};

static void gui_rec_browse(void)
{
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_OUTPUT);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &gg->wrec.wrec, NULL, 0)))
		return;

	ffui_settextz(&gg->wrec.eout, fn);
}

static void gui_rec_action(ffui_wnd *wnd, int id)
{
	const struct cmd *cmd = getcmd(id, rec_cmds, FFCNT(rec_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	switch (id) {
	case CVT_SETS_EDIT: {
		int i = ffui_view_selnext(&gg->wrec.vsets, -1);
		ffui_view_edit(&gg->wrec.vsets, i, VSETS_VAL);
		break;
	}

	case CVT_SETS_EDITDONE: {
		int i = ffui_view_selnext(&gg->wrec.vsets, -1);
		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_settextz(&it, gg->wrec.vsets.text);
		ffui_view_set(&gg->wrec.vsets, VSETS_VAL, &it);
		break;
	}
	}
}

int gui_rec_addsetts(void *trk)
{
	if (gg->wrec_init) {
		rec_sets_destroy(&gg->rec_sets);
		ffstr s;
		ffui_textstr(&gg->wrec.eout, &s);
		gg->rec_sets.output = s.ptr;

		if (0 != gui_cvt_getsettings(rec_sets, FFCNT(rec_sets), &gg->rec_sets, &gg->wrec.vsets))
			return -1;
	}

	char *exp;
	if (NULL == (exp = ffenv_expand(NULL, 0, gg->rec_sets.output)))
		return -1;
	gg->track->setvalstr4(trk, "output", exp, FMED_TRK_FACQUIRE);

	fmed_trk *trkconf = gg->track->conf(trk);

	for (uint i = 0;  i != FFCNT(rec_sets);  i++) {

		void *p = (char*)&gg->rec_sets + (rec_sets[i].flags & 0xffff);

		if (rec_sets[i].flags & CVTF_STR) {
			char **pstr = p;
			if (*pstr == NULL)
				continue;
			gg->track->setvalstr4(trk, rec_sets[i].settname, *pstr, 0);
			continue;
		}

		int *pint = p;
		if (*pint == -1)
			continue;

		if (ffsz_eq(rec_sets[i].settname, "gain"))
			trkconf->audio.gain = *pint;
		else if (ffsz_eq(rec_sets[i].settname, "until_time"))
			trkconf->audio.until = *pint;
		else if (ffsz_eq(rec_sets[i].settname, "pcm_format"))
			trkconf->audio.fmt.format = *pint;
		else if (ffsz_eq(rec_sets[i].settname, "pcm_channels"))
			trkconf->audio.fmt.channels = *pint;
		else if (ffsz_eq(rec_sets[i].settname, "pcm_sample_rate"))
			trkconf->audio.fmt.sample_rate = *pint;

		gg->track->setval(trk, rec_sets[i].settname, *pint);
	}

	return 0;
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
	ffui_settextz(&gg->wabout.labout,
		"fmedia v" FMED_VER "\n\n"
		"Fast media player, recorder, converter");
	ffui_settextz(&gg->wabout.lurl, FMED_HOMEPAGE);
	gg->wabout.wabout.hide_on_close = 1;
	gg->wabout.wabout.on_action = &gui_wabout_action;
}

static void gui_wabout_action(ffui_wnd *wnd, int id)
{
	switch (id) {
	case OPEN_HOMEPAGE: {
		ssize_t i = (ssize_t)ShellExecute(NULL, TEXT("open"), TEXT(FMED_HOMEPAGE), NULL, NULL, SW_SHOWNORMAL);
		if (i <= 32)
			syserrlog(core, NULL, "gui", "ShellExecute()");
		break;
	}
	}
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
