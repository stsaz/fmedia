/**
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>
#include <gui-winapi/gui.h>
#include <FF/time.h>
#include <FFOS/process.h>

typedef struct cvt_sets_t {
	uint init :1;

	char *_output[7];
	ffslice output; // char*[]

	union {
	int vorbis_quality;
	float vorbis_quality_f;
	};
	uint opus_bitrate;
	int opus_frsize;
	int opus_bandwidth;
	int mpg_quality;
	int aac_quality;
	int aac_bandwidth;
	int flac_complevel;
	int flac_md5;
	int stream_copy;

	int format;
	int conv_pcm_rate;
	int channels;
	union {
	int gain;
	float gain_f;
	};
	char *meta;
	int seek;
	int until;

	int overwrite;
	int out_preserve_date;
} cvt_sets_t;

struct gui_wconvert {
	ffui_wnd wconvert;
	ffui_menu mmconv;
	ffui_label lfn, lsets;
	ffui_combx eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnout;

	cvt_sets_t conv_sets;
	ffbyte conf_flags[1];
	int wconv_init :1;
};

const ffui_ldr_ctl wconvert_ctls[] = {
	FFUI_LDR_CTL(struct gui_wconvert, wconvert),
	FFUI_LDR_CTL(struct gui_wconvert, mmconv),
	FFUI_LDR_CTL(struct gui_wconvert, lfn),
	FFUI_LDR_CTL(struct gui_wconvert, lsets),
	FFUI_LDR_CTL(struct gui_wconvert, eout),
	FFUI_LDR_CTL(struct gui_wconvert, boutbrowse),
	FFUI_LDR_CTL(struct gui_wconvert, vsets),
	FFUI_LDR_CTL(struct gui_wconvert, pnout),
	FFUI_LDR_CTL_END
};

enum {
	VSETS_NAME,
	VSETS_VAL,
	VSETS_DESC,
};

static void gui_cvt_action(ffui_wnd *wnd, int id);
static void gui_conv_browse(void);
static void gui_convert(void);
static int conv_sets_add(char *val);


static const struct cmd cvt_cmds[] = {
	{ OUTBROWSE,	F0,	&gui_conv_browse },
	{ CONVERT,	F0 | CMD_FCORE,	&gui_convert },
};

static void gui_cvt_action(ffui_wnd *wnd, int id)
{
	struct gui_wconvert *w = gg->wconvert;
	const struct cmd *cmd = getcmd(id, cvt_cmds, FFCNT(cvt_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	switch (id) {
	case CVT_ACTIVATE: {
		char buf[255];
		ffs_fmt2(buf, sizeof(buf), "Convert %u file(s) to:%Z"
			, (int)wmain_list_n_selected());
		ffui_settextz(&w->lfn, buf);
		break;
	}

	case CVT_SETS_EDIT: {
		int i, isub;
		ffui_point pt;
		ffui_cur_pos(&pt);
		if (-1 == (i = ffui_view_hittest(&w->vsets, &pt, &isub))
			|| isub != VSETS_VAL)
			return;
		ffui_view_edit(&w->vsets, i, VSETS_VAL);
		}
		break;

	case CVT_SETS_EDITDONE: {
		int i = ffui_view_focused(&w->vsets);
		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_settextz(&it, w->vsets.text);
		ffui_view_set(&w->vsets, VSETS_VAL, &it);
		}
		break;
	}
}

static int sett_fromstr(const struct cvt_set *st, void *p, ffstr *data);

enum {
	SETT_DEV_LP,
	SETT_DEV_REC,

	SETT_FMT,
	SETT_RATE,
	SETT_CHAN,
	SETT_CONVFMT,
	SETT_CONVRATE,
	SETT_CONVCHAN,

	SETT_GAIN,
	SETT_UNTIL,
	SETT_SEEK,

	SETT_AAC_QUAL,
	SETT_AAC_BANDWIDTH,
	SETT_VORB_QUAL,
	SETT_OPUS_BRATE,
	SETT_OPUS_FRSIZE,
	SETT_OPUS_BANDWIDTH,
	SETT_MPEG_QUAL,
	SETT_FLAC_COMP,
	SETT_FLAC_MD5,
	SETT_DATACOPY,

	SETT_META,
	SETT_OUT_PRESDATE,
	SETT_OUT_OVWR,
};

static void cvt_prop_set(fmed_trk *t, uint name, int64 val)
{
	switch (name) {

	case SETT_FMT:
		t->audio.fmt.format = val; break;
	case SETT_RATE:
		t->audio.fmt.sample_rate = val; break;
	case SETT_CHAN:
		t->audio.fmt.channels = val; break;
	case SETT_CONVFMT:
		t->audio.convfmt.format = val; break;
	case SETT_CONVCHAN:
		t->audio.convfmt.channels = val; break;
	case SETT_CONVRATE:
		t->audio.convfmt.sample_rate = val; break;

	case SETT_GAIN:
		t->audio.gain = val; break;
	case SETT_UNTIL:
		t->audio.until = val; break;
	case SETT_SEEK:
		t->audio.seek = val; break;

	case SETT_AAC_QUAL:
		t->aac.quality = val; break;
	case SETT_AAC_BANDWIDTH:
		t->aac.bandwidth = val; break;
	case SETT_VORB_QUAL:
		t->vorbis.quality = val + 10; break;
	case SETT_OPUS_BRATE:
		t->opus.bitrate = val; break;
	case SETT_OPUS_FRSIZE:
		t->opus.frame_size = val; break;
	case SETT_OPUS_BANDWIDTH:
		t->opus.bandwidth = val; break;
	case SETT_MPEG_QUAL:
		t->mpeg.quality = val; break;
	case SETT_FLAC_COMP:
		t->flac.compression = val; break;
	case SETT_FLAC_MD5:
		t->flac.md5 = val; break;
	case SETT_DATACOPY:
		t->stream_copy = val; break;

	case SETT_OUT_PRESDATE:
		t->out_preserve_date = val; break;
	case SETT_OUT_OVWR:
		t->out_overwrite = val; break;
	}
}

static const char* const cvt_grps[] = {
	"Input",
	"Audio",
	"Codec",
	"Output",
};

// gui -> core
static const struct cvt_set cvt_sets[] = {
	{ SETT_SEEK, "Seek to", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | CVTF_NEWGRP | FFOFF(cvt_sets_t, seek) },
	{ SETT_UNTIL, "Stop at", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(cvt_sets_t, until) },

	{ SETT_CONVFMT, "Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_EMPTY | CVTF_NEWGRP | FFOFF(cvt_sets_t, format) },
	{ SETT_CONVRATE, "Sample rate (Hz)", "", CVTF_EMPTY | FFOFF(cvt_sets_t, conv_pcm_rate) },
	{ SETT_CONVCHAN, "Channels", "2 (stereo) | 1 (mono) | left | right", CVTF_EMPTY | FFOFF(cvt_sets_t, channels) },
	{ SETT_GAIN, "Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_EMPTY | FFOFF(cvt_sets_t, gain) },

	{ SETT_VORB_QUAL, "Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 | CVTF_NEWGRP | FFOFF(cvt_sets_t, vorbis_quality) },
	{ SETT_OPUS_BRATE, "Opus Bitrate", "6..510", FFOFF(cvt_sets_t, opus_bitrate) },
	{ SETT_OPUS_FRSIZE, "Opus Frame Size (msec)", "", CVTF_EMPTY | FFOFF(cvt_sets_t, opus_frsize) },
	{ SETT_OPUS_BANDWIDTH, "Opus Frequency Cut-off (KHz)", "4, 6, 8, 12 or 20", CVTF_EMPTY | FFOFF(cvt_sets_t, opus_bandwidth) },
	{ SETT_MPEG_QUAL, "MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", FFOFF(cvt_sets_t, mpg_quality) },
	{ SETT_AAC_QUAL, "AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", FFOFF(cvt_sets_t, aac_quality) },
	{ SETT_AAC_BANDWIDTH, "AAC Frequency Cut-off (Hz)", "max=20000", CVTF_EMPTY | FFOFF(cvt_sets_t, aac_bandwidth) },
	{ SETT_FLAC_COMP, "FLAC Compression", "0..8", FFOFF(cvt_sets_t, flac_complevel) },
	{ SETT_FLAC_MD5, "FLAC: generate MD5 checksum of uncompressed data", "0 or 1", CVTF_EMPTY | FFOFF(cvt_sets_t, flac_md5) },
	{ SETT_DATACOPY, "Stream copy", "Don't re-encode OGG/MP3 data (0 or 1)", FFOFF(cvt_sets_t, stream_copy) },

	{ SETT_META, "Meta Tags", "[clear;]NAME=VAL;...", CVTF_STR | CVTF_EMPTY | CVTF_NEWGRP | FFOFF(cvt_sets_t, meta) },
	{ SETT_OUT_OVWR, "Overwrite Output File", "0 or 1", FFOFF(cvt_sets_t, overwrite) },
	{ SETT_OUT_PRESDATE, "Preserve Date", "0 or 1", FFOFF(cvt_sets_t, out_preserve_date) },
};

static int conf_conv_sets_output(ffparser_schem *ps, void *obj, char *val)
{
	conv_sets_add(val);
	return 0;
}

// conf -> gui
static const ffpars_arg cvt_sets_conf[] = {
	{ "output",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FLIST, FFPARS_DST(&conf_conv_sets_output) },

	{ "vorbis_quality",	FFPARS_TFLOAT, FFPARS_DSTOFF(cvt_sets_t, vorbis_quality_f) },
	{ "opus_bitrate",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, opus_bitrate) },
	{ "mpeg_quality",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, mpg_quality) },
	{ "aac_quality",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, aac_quality) },
	{ "aac_bandwidth",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, aac_bandwidth) },
	{ "flac_complevel",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, flac_complevel) },

	{ "pcm_rate",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, conv_pcm_rate) },
	{ "gain",	FFPARS_TFLOAT, FFPARS_DSTOFF(cvt_sets_t, gain_f) },

	{ "overwrite",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, overwrite) },
	{ "preserve_date",	FFPARS_TINT, FFPARS_DSTOFF(cvt_sets_t, out_preserve_date) },
};

static void gui_cvt_sets_init(cvt_sets_t *sets)
{
	struct gui_wconvert *w = gg->wconvert;
	ffslice_set(&w->conv_sets.output, w->conv_sets._output, 0);

	sets->init = 1;
	sets->format = sets->conv_pcm_rate = sets->channels = SETT_EMPTY_INT;
	sets->gain_f = 0.0;
	sets->seek = sets->until = SETT_EMPTY_INT;

	sets->vorbis_quality_f = 5.0;
	sets->opus_bitrate = 128;
	sets->opus_frsize = SETT_EMPTY_INT;
	sets->opus_bandwidth = SETT_EMPTY_INT;
	sets->mpg_quality = 2;
	sets->aac_quality = 192;
	sets->aac_bandwidth = SETT_EMPTY_INT;
	sets->flac_complevel = 6;
	sets->flac_md5 = SETT_EMPTY_INT;
	sets->stream_copy = 0;
	sets->overwrite = 0;
	sets->out_preserve_date = 1;
}

/** Add new entry to the end of list.
If entry exists, remove and re-add it. */
static int conv_sets_add(char *val)
{
	struct gui_wconvert *w = gg->wconvert;
	ssize_t i = ffszarr_find((void*)w->conv_sets.output.ptr, w->conv_sets.output.len, val, ffsz_len(val));
	if (i == -1
		&& w->conv_sets.output.len == FFCNT(w->conv_sets._output))
		i = 0;

	if (i != -1) {
		char *old = *ffslice_itemT(&w->conv_sets.output, i, char*);
		ffmem_free(old);
		ffslice_rmT(&w->conv_sets.output, i, 1, char*);
	}

	char **it = ffslice_pushT(&w->conv_sets.output, FF_COUNT(w->conv_sets._output), char*);
	*it = val;
	return i;
}

/** Write all convert.output file names to config. */
void conv_sets_write(ffconfw *conf)
{
	struct gui_wconvert *w = gg->wconvert;
	char **it;
	FFSLICE_WALK_T(&w->conv_sets.output, it, char*) {
		ffconf_writez(conf, *it, FFCONF_TVAL);
	}
}

int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	struct gui_wconvert *w = gg->wconvert;
	ffpars_setargs(ctx, &w->conv_sets, cvt_sets_conf, FFCNT(cvt_sets_conf));
	return 0;
}

/** Setting's value -> string. */
int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst)
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

		if ((sett->flags & CVTF_EMPTY) && i == SETT_EMPTY_INT)
			return 0;

		ffstr_catfmt(dst, "%d", i);
	}
	return 0;
}

/** Setting's string value -> internal representation. */
static int sett_fromstr(const struct cvt_set *st, void *p, ffstr *data)
{
	int *pint = p;
	int val;
	double d;

	if (st->flags & CVTF_STR) {
		char **pstr = p;
		if ((st->flags & CVTF_EMPTY) && data->len == 0) {
			*pstr = NULL;
			return 0;
		}
		*pstr = data->ptr;
		return 1;
	}

	if ((st->flags & CVTF_EMPTY) && data->len == 0) {
		*pint = SETT_EMPTY_INT;
		return 0;
	}

	if (st->flags & CVTF_MSEC) {
		ffdtm dt;
		fftime t;
		if (data->len != fftime_fromstr(&dt, data->ptr, data->len, FFTIME_HMS_MSEC_VAR))
			return -1;

		fftime_join(&t, &dt, FFTIME_TZNODATE);
		val = fftime_ms(&t);

	} else if (st->flags & CVTF_FLT) {
		if (data->len != ffs_tofloat(data->ptr, data->len, &d, 0))
			return -1;

		if (st->flags & CVTF_FLT10)
			val = d * 10;
		else if (st->flags & CVTF_FLT100)
			val = d * 100;

	} else if (st->settname == SETT_FMT || st->settname == SETT_CONVFMT) {
		if (0 > (val = ffpcm_fmt(data->ptr, data->len)))
			return -1;

	} else if (st->settname == SETT_CHAN || st->settname == SETT_CONVCHAN) {
		if (0 > (val = ffpcm_channels(data->ptr, data->len)))
			return -1;

	} else {
		if (data->len != ffs_toint(data->ptr, data->len, &val, FFS_INT32))
			return -1;
	}

	*pint = val;
	return 0;
}

static void conv_sets_reset(cvt_sets_t *sets)
{
	ffmem_safefree0(sets->meta);
}

static void cvt_sets_destroy(cvt_sets_t *sets)
{
	char **it;
	FFSLICE_WALK_T(&sets->output, it, char*) {
		ffmem_free0(*it);
	}
	conv_sets_reset(sets);
}

void wconv_show(uint show)
{
	struct gui_wconvert *w = gg->wconvert;

	if (!show) {
		ffui_show(&w->wconvert, 0);
		return;
	}

	if (!w->wconv_init) {
		if (!w->conv_sets.init)
			gui_cvt_sets_init(&w->conv_sets);

		// initialize wconvert.eout
		if (w->conv_sets.output.len != 0)
			ffui_settextz(&w->eout, *ffslice_lastT(&w->conv_sets.output, char*));
		char **iter;
		FFSLICE_RWALK_T(&w->conv_sets.output, iter, char*) {
			ffui_combx_insz(&w->eout, -1, *iter);
		}

		ffui_view_showgroups(&w->vsets, 1);
		const char *const *grp;
		int grp_id = 0;
		ffui_viewgrp vg;
		FFARRS_FOREACH(cvt_grps, grp) {
			ffui_viewgrp_reset(&vg);
			ffui_viewgrp_settextz(&vg, *grp);
			ffui_view_insgrp(&w->vsets, -1, grp_id++, &vg);
		}
		grp_id = 0;

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffarr s = {0};

		uint i;
		for (i = 0;  i != FFCNT(cvt_sets);  i++) {

			if ((cvt_sets[i].flags & CVTF_NEWGRP) && i != 0)
				grp_id++;
			ffui_view_setgroupid(&it, grp_id);

			ffui_view_settextz(&it, cvt_sets[i].name);
			ffui_view_append(&w->vsets, &it);

			s.len = 0;
			sett_tostr(&w->conv_sets, &cvt_sets[i], &s);
			ffui_view_settextstr(&it, &s);
			ffui_view_set(&w->vsets, VSETS_VAL, &it);

			ffui_view_settextz(&it, cvt_sets[i].desc);
			ffui_view_set(&w->vsets, VSETS_DESC, &it);

		}

		ffarr_free(&s);
		w->wconv_init = 1;
	}

	ffui_show(&w->wconvert, 1);
	ffui_wnd_setfront(&w->wconvert);
}

void gui_setconvpos(uint cmd)
{
	struct gui_wconvert *w = gg->wconvert;
	char buf[64];
	int pos = wmain_curpos();
	int r = ffs_fmt2(buf, sizeof(buf), "%02u:%02u", pos / 60, pos % 60);
	if (r <= 0)
		return;

	ffui_viewitem it;
	ffui_view_iteminit(&it);

	uint name = (cmd == SETCONVPOS_SEEK) ? SETT_SEEK : SETT_UNTIL;
	uint i;
	for (i = 0;  i != FFCNT(cvt_sets);  i++) {
		if (cvt_sets[i].settname == name)
			break;
	}

	ffui_view_setindex(&it, i);
	ffui_view_settext(&it, buf, r);
	ffui_view_set(&w->vsets, VSETS_VAL, &it);
	ffui_view_itemreset(&it);
}


/** Convert settings from GUI to the representation specified by their format. */
int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist)
{
	uint k;
	int val, rc = -1;
	char *txt;
	size_t len;
	const struct cvt_set *st;
	ffui_viewitem it;
	ffui_view_iteminit(&it);
	for (k = 0;  k != nsets;  k++) {
		st = &psets[k];

		ffui_view_setindex(&it, k);
		ffui_view_gettext(&it);
		ffui_view_get(vlist, VSETS_VAL, &it);

		if (NULL == (txt = ffsz_alcopyqz(ffui_view_textq(&it)))) {
			syserrlog(core, NULL, "gui", "%s", ffmem_alloc_S);
			ffui_view_itemreset(&it);
			return -1;
		}
		len = ffsz_len(txt);

		void *p = (char*)sets + (st->flags & 0xffff);
		ffstr data;
		ffstr_set(&data, txt, len);
		val = sett_fromstr(st, p, &data);
		if (val < 0)
			goto end;
		else if (val > 0)
			continue;

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
	struct gui_wconvert *w = gg->wconvert;
	int i;
	fmed_que_entry e, *qent, *inp;
	ffstr fn, name;
	uint k;
	ffarr ar = {0};

	conv_sets_reset(&w->conv_sets);

	ffui_textstr(&w->eout, &fn);
	if (fn.len == 0 || 0 == wmain_list_n_selected()) {
		errlog(core, NULL, "gui", "convert: no files selected");
		ffstr_free(&fn);
		goto end;
	}

	// update output file names list
	i = conv_sets_add(fn.ptr);
	if (i != -1)
		ffui_combx_rm(&w->eout, w->conv_sets.output.len - i - 1);
	ffui_combx_insz(&w->eout, 0, fn.ptr);
	ffui_combx_set(&w->eout, 0);

	if (0 != gui_cvt_getsettings(cvt_sets, FFCNT(cvt_sets), &w->conv_sets, &w->vsets))
		goto end;

	int curtab = wmain_tab_active();
	int itab = gg->itab_convert;
	if (itab == -1) {
		itab = gui_newtab(GUI_TAB_CONVERT);
		gg->qu->cmdv(FMED_QUE_NEW, FMED_QUE_NORND);
		gg->itab_convert = itab;
	} else {
		wmain_tab_activate(itab);
	}

	i = -1;
	while (-1 != (i = wmain_list_next_selected(i))) {
		inp = (fmed_que_entry*)gg->qu->fmed_queue_item(curtab, i);

		ffmem_tzero(&e);
		e.url = inp->url;
		e.from = inp->from;
		e.to = inp->to;
		if (NULL == (qent = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, itab, &e))) {
			continue;
		}

		if (NULL == _ffarr_append(&ar, &qent, 1, sizeof(qent)))
			goto end;

		ffstr sname, *sval;
		size_t n;
		for (n = 0;  NULL != (sval = gg->qu->meta(inp, n, &sname, FMED_QUE_NO_TMETA));  n++) {
			if (sval == FMED_QUE_SKIP)
				continue;
			gg->qu->meta_set(qent, sname.ptr, sname.len, sval->ptr, sval->len, 0);
		}

		gg->qu->meta_set(qent, FFSTR("output"), fn.ptr, fn.len, FMED_QUE_TRKDICT);

		fmed_trk trkprops;
		gg->track->copy_info(&trkprops, NULL);
		for (k = 0;  k != FFCNT(cvt_sets);  k++) {

			void *p = (char*)&w->conv_sets + (cvt_sets[k].flags & 0xffff);

			if (cvt_sets[k].flags & CVTF_STR) {
				switch (cvt_sets[k].settname) {
				case SETT_META:
					ffstr_setcz(&name, "meta"); break;
				}
				char **pstr = p;
				if (*pstr == NULL)
					continue;
				gg->qu->meta_set(qent, name.ptr, name.len, *pstr, ffsz_len(*pstr), FMED_QUE_TRKDICT);
				continue;
			}

			int *pint = p;
			if (*pint == SETT_EMPTY_INT)
				continue;

			cvt_prop_set(&trkprops, cvt_sets[k].settname, *pint);
		}
		gg->qu->cmdv(FMED_QUE_SETTRACKPROPS, qent, &trkprops);
	}

	gui_showque(itab);

	if (ar.len != 0) {
		qent = *(void**)ar.ptr;
		gg->qu->cmdv(FMED_QUE_XPLAY, qent);
	}

end:
	ffarr_free(&ar);
}

static void gui_conv_browse(void)
{
	struct gui_wconvert *w = gg->wconvert;
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_OUTPUT);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &w->wconvert, NULL, 0)))
		return;

	ffui_settextz(&w->eout, fn);
}

static void conv_writeval(ffconfw *conf, ffuint i)
{
	// struct gui_wconvert *w = gg->wconvert;
	switch (i) {
	case 0:
		conv_sets_write(conf);
		break;
	}
}

int wconvert_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wconvert *w = gg->wconvert;
	static const char setts[][23] = {
		"gui.gui.convert.output",
	};

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
			if (!w->conf_flags[i]) {
				ffconf_writez(conf, setts[i], FFCONF_TKEY | FFCONF_ASIS);
				conv_writeval(conf, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
		if (ffstr_matchz(line, setts[i])) {
			ffconf_writez(conf, setts[i], FFCONF_TKEY | FFCONF_ASIS);
			conv_writeval(conf, i);
			w->conf_flags[i] = 1;
			return 1;
		}
	}
	return 0;
}

void wconvert_init()
{
	struct gui_wconvert *w = ffmem_new(struct gui_wconvert);
	gg->wconvert = w;
	w->wconvert.hide_on_close = 1;
	w->wconvert.onactivate_id = CVT_ACTIVATE;
	w->wconvert.on_action = &gui_cvt_action;
	w->vsets.edit_id = CVT_SETS_EDITDONE;
	gui_cvt_sets_init(&w->conv_sets);
}

void wconvert_destroy()
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_destroy(&w->conv_sets);
	ffmem_free0(gg->wconvert);
}
