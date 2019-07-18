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
static int conv_sets_add(char *val);

static void gui_rec_init(void);
static void gui_rec_action(ffui_wnd *wnd, int id);
static void gui_rec_browse(void);

static void gui_dev_action(ffui_wnd *wnd, int id);

static void gui_info_action(ffui_wnd *wnd, int id);

static void gui_wgoto_action(ffui_wnd *wnd, int id);

static void gui_wabout_action(ffui_wnd *wnd, int id);

static void gui_wuri_action(ffui_wnd *wnd, int id);

static void gui_wfilter_action(ffui_wnd *wnd, int id);


void wconvert_init()
{
	gg->wconvert.wconvert.hide_on_close = 1;
	gg->wconvert.wconvert.onactivate_id = CVT_ACTIVATE;
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
	case CVT_ACTIVATE: {
		char buf[255];
		ffs_fmt2(buf, sizeof(buf), "Convert %u file(s) to:%Z"
			, (int)ffui_view_selcount(&gg->wmain.vlist));
		ffui_settextz(&gg->wconvert.lfn, buf);
		break;
	}

	case CVT_SETS_EDIT: {
		int i, isub;
		ffui_point pt;
		ffui_cur_pos(&pt);
		if (-1 == (i = ffui_view_hittest(&gg->wconvert.vsets, &pt, &isub))
			|| isub != VSETS_VAL)
			return;
		ffui_view_edit(&gg->wconvert.vsets, i, VSETS_VAL);
		}
		break;

	case CVT_SETS_EDITDONE: {
		int i = ffui_view_focused(&gg->wconvert.vsets);
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
	CVTF_NEWGRP = 0x400000,
};

#define SETT_EMPTY_INT  ((int)0x80000000)

struct cvt_set {
	uint settname;
	const char *name;
	const char *desc;
	uint flags; //enum CVTF
};

static int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst);
static int sett_fromstr(const struct cvt_set *st, void *p, ffstr *data);
static int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist);

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

void gui_cvt_sets_init(cvt_sets_t *sets)
{
	ffarr2_set(&gg->conv_sets.output, gg->conv_sets._output, 0);

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
	ssize_t i = ffszarr_find((void*)gg->conv_sets.output.ptr, gg->conv_sets.output.len, val, ffsz_len(val));
	if (i == -1
		&& gg->conv_sets.output.len == FFCNT(gg->conv_sets._output))
		i = 0;

	if (i != -1) {
		ffmem_free(gg->conv_sets.output.ptr[i]);
		ffarr2_rm_shift(&gg->conv_sets.output, i);
	}

	char **it = ffarr2_push(&gg->conv_sets.output);
	*it = val;
	return i;
}

/** Write all convert.output file names to config. */
void conv_sets_write(ffconfw *conf)
{
	char **it;
	FFARR2_WALK(&gg->conv_sets.output, it) {
		ffconf_writez(conf, *it, FFCONF_TVAL);
	}
}

int gui_conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &gg->conv_sets, cvt_sets_conf, FFCNT(cvt_sets_conf));
	return 0;
}

/** Setting's value -> string. */
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

void cvt_sets_destroy(cvt_sets_t *sets)
{
	char **it;
	FFARR2_WALK(&sets->output, it) {
		ffmem_free0(*it);
	}
	conv_sets_reset(sets);
}

void gui_showconvert(void)
{
	if (!gg->wconv_init) {
		if (!gg->conv_sets.init)
			gui_cvt_sets_init(&gg->conv_sets);

		// initialize wconvert.eout
		if (gg->conv_sets.output.len != 0)
			ffui_settextz(&gg->wconvert.eout, *ffarr2_last(&gg->conv_sets.output));
		char **iter;
		FFARR2_RWALK(&gg->conv_sets.output, iter) {
			ffui_combx_insz(&gg->wconvert.eout, -1, *iter);
		}

		ffui_view_showgroups(&gg->wconvert.vsets, 1);
		const char *const *grp;
		int grp_id = 0;
		ffui_viewgrp vg;
		FFARRS_FOREACH(cvt_grps, grp) {
			ffui_viewgrp_reset(&vg);
			ffui_viewgrp_settextz(&vg, *grp);
			ffui_view_insgrp(&gg->wconvert.vsets, -1, grp_id++, &vg);
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

	uint name = (cmd == SETCONVPOS_SEEK) ? SETT_SEEK : SETT_UNTIL;
	uint i;
	for (i = 0;  i != FFCNT(cvt_sets);  i++) {
		if (cvt_sets[i].settname == name)
			break;
	}

	ffui_view_setindex(&it, i);
	ffui_view_settext(&it, buf, r);
	ffui_view_set(&gg->wconvert.vsets, VSETS_VAL, &it);
	ffui_view_itemreset(&it);
}


/** Convert settings from GUI to the representation specified by their format. */
static int gui_cvt_getsettings(const struct cvt_set *psets, uint nsets, void *sets, ffui_view *vlist)
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
	int i;
	fmed_que_entry e, *qent, *inp;
	ffstr fn, name;
	uint k;
	ffarr ar = {0};

	conv_sets_reset(&gg->conv_sets);

	ffui_textstr(&gg->wconvert.eout, &fn);
	if (fn.len == 0 || 0 == ffui_view_selcount(&gg->wmain.vlist)) {
		errlog(core, NULL, "gui", "convert: no files selected");
		return;
	}

	// update output file names list
	i = conv_sets_add(fn.ptr);
	if (i != -1)
		ffui_combx_rm(&gg->wconvert.eout, gg->conv_sets.output.len - i - 1);
	ffui_combx_insz(&gg->wconvert.eout, 0, fn.ptr);
	ffui_combx_set(&gg->wconvert.eout, 0);

	if (0 != gui_cvt_getsettings(cvt_sets, FFCNT(cvt_sets), &gg->conv_sets, &gg->wconvert.vsets))
		goto end;

	int curtab = ffui_tab_active(&gg->wmain.tabs);
	int itab = gg->itab_convert;
	if (itab == -1) {
		itab = gui_newtab(GUI_TAB_CONVERT);
		gg->qu->cmdv(FMED_QUE_NEW, FMED_QUE_NORND);
		gg->itab_convert = itab;
	} else {
		ffui_tab_setactive(&gg->wmain.tabs, itab);
	}

	i = -1;
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, i))) {
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

			void *p = (char*)&gg->conv_sets + (cvt_sets[k].flags & 0xffff);

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

static void rec_prop_set(void *trk, fmed_trk *t, uint name, int64 val)
{
	switch (name) {

	case SETT_DEV_LP:
		gg->track->setval(trk, "loopback_device", val); break;
	case SETT_DEV_REC:
		gg->track->setval(trk, "capture_device", val); break;

	case SETT_FMT:
		t->audio.fmt.format = val; break;
	case SETT_RATE:
		t->audio.fmt.sample_rate = val; break;
	case SETT_CHAN:
		t->audio.fmt.channels = val; break;

	case SETT_GAIN:
		t->audio.gain = val; break;
	case SETT_UNTIL:
		t->audio.until = val; break;

	case SETT_AAC_QUAL:
		t->aac.quality = val; break;
	case SETT_VORB_QUAL:
		t->vorbis.quality = val + 10; break;
	case SETT_OPUS_BRATE:
		t->opus.bitrate = val; break;
	case SETT_MPEG_QUAL:
		t->mpeg.quality = val; break;
	case SETT_FLAC_COMP:
		t->flac.compression = val; break;
	}
}

static const char* const rec_grps[] = {
	"Input",
	"Audio",
	"Codec",
};

// gui -> core
static const struct cvt_set rec_sets[] = {
	{ SETT_DEV_LP, "Loopback Device Number (record from playback)", "empty: disabled;  0: default device", CVTF_EMPTY | CVTF_NEWGRP | FFOFF(rec_sets_t, lpbk_devno) },
	{ SETT_DEV_REC, "Capture Device Number", "0: default", CVTF_EMPTY | FFOFF(rec_sets_t, devno) },
	{ SETT_FMT, "Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_EMPTY | FFOFF(rec_sets_t, format) },
	{ SETT_RATE, "Sample Rate (Hz)", "", CVTF_EMPTY | FFOFF(rec_sets_t, sample_rate) },
	{ SETT_CHAN, "Channels", "2 (stereo) | 1 (mono) | left | right", CVTF_EMPTY | FFOFF(rec_sets_t, channels) },
	{ SETT_UNTIL, "Stop after", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_EMPTY | FFOFF(rec_sets_t, until) },

	{ SETT_GAIN, "Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_EMPTY | CVTF_NEWGRP | FFOFF(rec_sets_t, gain) },

	{ SETT_VORB_QUAL, "Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 | CVTF_NEWGRP | FFOFF(rec_sets_t, vorbis_quality) },
	{ SETT_OPUS_BRATE, "Opus Bitrate", "6..510", FFOFF(rec_sets_t, opus_bitrate) },
	{ SETT_MPEG_QUAL, "MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", FFOFF(rec_sets_t, mpg_quality) },
	{ SETT_AAC_QUAL, "AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", FFOFF(rec_sets_t, aac_quality) },
	{ SETT_FLAC_COMP, "FLAC Compression", "0..8", FFOFF(rec_sets_t, flac_complevel) },
};

// conf -> gui
static const ffpars_arg rec_sets_conf[] = {
	{ "output",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DSTOFF(rec_sets_t, output) },
	{ "loopback_device",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, lpbk_devno) },
	{ "capture_device",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, devno) },
	{ "gain",	FFPARS_TFLOAT, FFPARS_DSTOFF(rec_sets_t, gain_f) },

	{ "vorbis_quality",	FFPARS_TFLOAT, FFPARS_DSTOFF(rec_sets_t, vorbis_quality_f) },
	{ "opus_bitrate",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, opus_bitrate) },
	{ "mpeg_quality",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, mpg_quality) },
	{ "aac_quality",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, aac_quality) },
	{ "flac_complevel",	FFPARS_TINT, FFPARS_DSTOFF(rec_sets_t, flac_complevel) },
};

void rec_sets_init(rec_sets_t *sets)
{
	sets->init = 1;
	sets->format = sets->sample_rate = sets->channels = SETT_EMPTY_INT;
	sets->gain_f = 0.0;
	sets->devno = 0;
	sets->lpbk_devno = SETT_EMPTY_INT;
	sets->until = SETT_EMPTY_INT;

	sets->vorbis_quality_f = 5.0;
	sets->opus_bitrate = 128;
	sets->mpg_quality = 2;
	sets->aac_quality = 192;
	sets->flac_complevel = 6;
}

void rec_sets_destroy(rec_sets_t *sets)
{
	ffmem_safefree0(sets->output);
}

int gui_conf_rec(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &gg->rec_sets, rec_sets_conf, FFCNT(rec_sets_conf));
	return 0;
}

static void gui_rec_init(void)
{
	if (gg->wrec_init)
		return;

	if (!gg->rec_sets.init)
		rec_sets_init(&gg->rec_sets);

	if (ffui_textlen(&gg->wrec.eout) == 0)
		ffui_settextz(&gg->wrec.eout, gg->rec_sets.output);

	ffui_view_showgroups(&gg->wrec.vsets, 1);
	const char *const *grp;
	int grp_id = 0;
	ffui_viewgrp vg;
	FFARRS_FOREACH(rec_grps, grp) {
		ffui_viewgrp_reset(&vg);
		ffui_viewgrp_settextz(&vg, *grp);
		ffui_view_insgrp(&gg->wrec.vsets, -1, grp_id++, &vg);
	}
	grp_id = 0;

	ffui_viewitem it;
	ffui_view_iteminit(&it);
	ffarr s = {0};

	uint i;
	for (i = 0;  i != FFCNT(rec_sets);  i++) {

		if ((rec_sets[i].flags & CVTF_NEWGRP) && i != 0)
			grp_id++;
		ffui_view_setgroupid(&it, grp_id);

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

void gui_rec_show(void)
{
	gui_rec_init();
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
		int i, isub;
		ffui_point pt;
		ffui_cur_pos(&pt);
		if (-1 == (i = ffui_view_hittest(&gg->wrec.vsets, &pt, &isub))
			|| isub != VSETS_VAL)
			return;
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
	if (NULL == (exp = core->env_expand(NULL, 0, gg->rec_sets.output)))
		return -1;
	gg->track->setvalstr4(trk, "output", exp, FMED_TRK_FACQUIRE);

	fmed_trk *trkconf = gg->track->conf(trk);

	for (uint i = 0;  i != FFCNT(rec_sets);  i++) {

		void *p = (char*)&gg->rec_sets + (rec_sets[i].flags & 0xffff);

		/*if (rec_sets[i].flags & CVTF_STR) {
			char **pstr = p;
			if (*pstr == NULL)
				continue;
			gg->track->setvalstr4(trk, , *pstr, 0);
			continue;
		}*/

		int *pint = p;
		if (*pint == SETT_EMPTY_INT)
			continue;

		rec_prop_set(trk, trkconf, rec_sets[i].settname, *pint);
	}

	return 0;
}


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
		ffui_viewitem it = {};
		char buf[64];
		uint n = ffs_fromint(idev, buf, sizeof(buf), 0);

		gui_rec_init();

		uint i;
		for (i = 0;  i != FFCNT(rec_sets);  i++) {
			if (rec_sets[i].settname == SETT_DEV_REC)
				break;
		}

		ffui_view_setindex(&it, i);
		ffui_view_settext(&it, buf, n);
		ffui_view_set(&gg->wrec.vsets, VSETS_VAL, &it);

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

void gui_media_showinfo(void)
{
	fmed_que_entry *e;
	ffui_viewitem it;
	ffui_viewgrp vg;
	int i, grp = 0;
	ffstr name, *val;

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

	for (i = 0;  NULL != (val = gg->qu->meta(e, i, &name, 0));  i++) {
		if (val == FMED_QUE_SKIP)
			continue;
		ffui_view_iteminit(&it);
		ffui_view_settextstr(&it, &name);
		ffui_view_setgroupid(&it, grp);
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
		ffui_trk_set(&gg->wmain.tpos, fftime_sec(&t));
		gui_seek(GOTO);
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
