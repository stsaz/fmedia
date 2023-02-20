/** fmedia: GUI-winapi: Convert dialog
Copyright (c) 2020 Simon Zolin */

#include <gui-winapi/gui.h>
#include <FFOS/process.h>

typedef struct cvt_sets_t {
	uint init :1;

	int seek;
	int until;

	int format;
	int conv_pcm_rate;
	int channels;

	int gain;
	float gain_f;

	int stream_copy;
	int aac_quality;
	int aac_bandwidth;
	int flac_complevel;
	int flac_md5;
	int mpg_quality;
	int opus_bitrate;
	int opus_bandwidth;
	int opus_frsize;
	int vorbis_quality;
	float vorbis_quality_f;

	char *meta;
	int overwrite;
	int out_preserve_date;
	char *_output[7];
	ffslice output; // char*[]
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
	ffbyte *wconf_used;
	uint vsets_edit_idx;
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

static int output_add(char *val);

/**
"int16" -> FFPCM_16LE; "" -> default */
static int conf_format(fmed_conf *fc, cvt_sets_t *cs, ffstr *s)
{
	int r = ffpcm_fmt(s->ptr, s->len);
	if (r < 0) {
		if (s->len != 0)
			return FMC_EBADVAL;
		r = SETT_EMPTY_INT;
	}

	cs->format = r;
	return 0;
}

static int conf_channels(fmed_conf *fc, cvt_sets_t *cs, ffstr *s)
{
	int r = ffpcm_channels(s->ptr, s->len);
	if (r < 0) {
		if (s->len != 0)
			return FMC_EBADVAL;
		r = SETT_EMPTY_INT;
	}

	cs->channels = r;
	return 0;
}

static int conf_conv_sets_output(fmed_conf *fc, cvt_sets_t *cs, char *val)
{
	output_add(val);
	return 0;
}

static int conf_seekuntil(fmed_conf *fc, cvt_sets_t *cs, ffstr *s)
{
	ffdatetime dt;
	fftime t;
	if (s->len != fftime_fromstr1(&dt, s->ptr, s->len, FFTIME_HMS_MSEC_VAR))
		return FMC_EBADVAL;
	fftime_join1(&t, &dt);

	if (ffsz_eq(fc->arg->name, "seek"))
		cs->seek = fftime_to_msec(&t);
	else
		cs->until = fftime_to_msec(&t);

	return 0;
}

static int conf_fin()
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_t *cs = &w->conv_sets;

	if (cs->gain_f != 0)
		cs->gain = cs->gain_f * 100;

	if (cs->vorbis_quality_f != -255)
		cs->vorbis_quality = cs->vorbis_quality_f * 10;

	return 0;
}

static const char* const cvt_grps[] = {
	"Input",
	"Audio Format",
	"Audio Filters",
	"Codec",
	"Output",
};

enum {
	SETT_SEEK,
	SETT_UNTIL,

	SETT_CONVFMT,
	SETT_CONVRATE,
	SETT_CONVCHAN,

	SETT_GAIN,

	SETT_DATACOPY,
	SETT_AAC_QUAL,
	SETT_AAC_BANDWIDTH,
	SETT_FLAC_COMP,
	SETT_MPEG_QUAL,
	SETT_OPUS_BRATE,
	SETT_OPUS_BANDWIDTH,
	SETT_OPUS_FRSIZE,
	SETT_VORB_QUAL,

	SETT_META,
	SETT_OUT_OVWR,
	SETT_OUT_PRESDATE,

	SETT_OUT,
};

#define O(a) FF_OFF(cvt_sets_t, a)
static const struct cvt_set settings[] = {
	{ SETT_SEEK,	O(seek),	"Seek to", "[MM:]SS[.MSC]", CVTF_MSEC | CVTF_NEWGRP },
	{ SETT_UNTIL,	O(until),	"Stop at", "[MM:]SS[.MSC]", CVTF_MSEC },

	{ SETT_CONVFMT,	O(format),	"Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_NEWGRP },
	{ SETT_CONVRATE,	O(conv_pcm_rate),	"Sample rate (Hz)", "", 0 },
	{ SETT_CONVCHAN,	O(channels),	"Channels", "2 (stereo) | 1 (mono) | left | right", 0 },

	{ SETT_GAIN,	O(gain),	"Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_NEWGRP },

	{ SETT_DATACOPY,	O(stream_copy),	"Stream copy", "Don't re-encode OGG/MP3 data (0 or 1)", CVTF_NEWGRP },
	{ SETT_AAC_QUAL,	O(aac_quality),	"AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", 0 },
	{ SETT_AAC_BANDWIDTH,	O(aac_bandwidth),	"AAC Frequency Cut-off (Hz)", "max=20000", 0 },
	{ SETT_FLAC_COMP,	O(flac_complevel),	"FLAC Compression", "0..8", 0 },
	{ SETT_MPEG_QUAL,	O(mpg_quality),	"MPEG Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", 0 },
	{ SETT_OPUS_BRATE,	O(opus_bitrate),	"Opus Bitrate", "6..510", 0 },
	{ SETT_OPUS_BANDWIDTH,	O(opus_bandwidth),	"Opus Frequency Cut-off (KHz)", "4, 6, 8, 12 or 20", 0 },
	{ SETT_OPUS_FRSIZE,	O(opus_frsize),	"Opus Frame Size (msec)", "", 0 },
	{ SETT_VORB_QUAL,	O(vorbis_quality),	"Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 },

	{ SETT_META,	O(meta),	"Meta Tags", "[clear;]NAME=VAL;...", CVTF_STR | CVTF_NEWGRP },
	{ SETT_OUT_OVWR,	O(overwrite),	"Overwrite Output File", "0 or 1", 0 },
	{ SETT_OUT_PRESDATE,	O(out_preserve_date),	"Preserve Date", "0 or 1", 0 },
	{ SETT_OUT,	0, NULL, NULL, 0 }
};

// conf -> gui
static const fmed_conf_arg cvt_sets_conf[] = {
	{ "seek",	FMC_STR, FMC_F(conf_seekuntil) },
	{ "until",	FMC_STR, FMC_F(conf_seekuntil) },

	{ "format",	FMC_INT32, FMC_F(conf_format) },
	{ "rate",	FMC_INT32, O(conv_pcm_rate) },
	{ "channels",	FMC_INT32, FMC_F(conf_channels) },

	{ "gain",	FMC_FLOAT32S, O(gain_f) },

	{ "data_copy",	FMC_INT32, O(stream_copy) },
	{ "aac_quality",	FMC_INT32, O(aac_quality) },
	{ "aac_bandwidth",	FMC_INT32, O(aac_bandwidth) },
	{ "flac_complevel",	FMC_INT32, O(flac_complevel) },
	{ "mpeg_quality",	FMC_INT32, O(mpg_quality) },
	{ "opus_bitrate",	FMC_INT32, O(opus_bitrate) },
	{ "opus_bandwidth",	FMC_INT32, O(opus_bandwidth) },
	{ "opus_frame_size",	FMC_INT32, O(opus_frsize) },
	{ "vorbis_quality",	FMC_FLOAT32S, O(vorbis_quality_f) },

	{ "meta",	FMC_STRZ, O(meta) },
	{ "overwrite",	FMC_INT32, O(overwrite) },
	{ "preserve_date",	FMC_INT32, O(out_preserve_date) },
	{ "output",	FMC_STRZ_LIST, FMC_F(conf_conv_sets_output) },
	{}
};
#undef O

static void gui_cvt_sets_init(cvt_sets_t *cs)
{
	struct gui_wconvert *w = gg->wconvert;
	ffslice_set(&w->conv_sets.output, w->conv_sets._output, 0);

	cs->seek = cs->until = SETT_EMPTY_INT;
	cs->format = cs->conv_pcm_rate = cs->channels = SETT_EMPTY_INT;
	cs->gain_f = 0.0;

	cs->stream_copy = SETT_EMPTY_INT;
	cs->aac_bandwidth = SETT_EMPTY_INT;
	cs->aac_quality = SETT_EMPTY_INT;
	cs->flac_complevel = SETT_EMPTY_INT;
	cs->flac_md5 = SETT_EMPTY_INT;
	cs->mpg_quality = SETT_EMPTY_INT;
	cs->opus_bandwidth = SETT_EMPTY_INT;
	cs->opus_bitrate = SETT_EMPTY_INT;
	cs->opus_frsize = SETT_EMPTY_INT;
	cs->vorbis_quality = SETT_EMPTY_INT;
	cs->vorbis_quality_f = -255;

	cs->overwrite = SETT_EMPTY_INT;
	cs->out_preserve_date = SETT_EMPTY_INT;
	cs->init = 1;
}

/** Add new entry to the end of list.
If entry exists, remove and re-add it. */
static int output_add(char *val)
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
	*it = ffsz_dup(val);
	return i;
}

/** Write all convert.output file names to config. */
static void output_write(ffconfw *conf)
{
	struct gui_wconvert *w = gg->wconvert;
	char **it;
	FFSLICE_WALK(&w->conv_sets.output, it) {
		ffconfw_addstrz(conf, *it);
	}
}

static void output_show()
{
	struct gui_wconvert *w = gg->wconvert;
	if (w->conv_sets.output.len != 0)
		ffui_settextz(&w->eout, *ffslice_lastT(&w->conv_sets.output, char*));

	char **it;
	FFSLICE_RWALK(&w->conv_sets.output, it) {
		ffui_combx_insz(&w->eout, -1, *it);
	}
}

int gui_conf_convert(fmed_conf *fc, void *obj)
{
	struct gui_wconvert *w = gg->wconvert;
	output_add("$filepath/$tracknumber. $artist - $title.ogg");
	output_add("$filepath/$filename.m4a");
	fmed_conf_addnewctx(fc, &w->conv_sets, cvt_sets_conf);
	return 0;
}

/** Setting's value -> string. */
int sett_tostr(const void *sets, const struct cvt_set *sett, ffarr *dst)
{
	void *src = (char*)sets + sett->field_off;
	if (sett->flags & CVTF_STR) {
		char **pstr = src;
		if (*pstr != NULL)
			ffvec_addsz(dst, *pstr);

	} else if (sett->flags & CVTF_FLT) {
		int i = *(int*)src;
		if (i == SETT_EMPTY_INT)
			return 0;

		if (NULL == ffarr_realloc(dst, FFINT_MAXCHARS + FFSLEN(".00")))
			return 0;
		if (sett->flags & CVTF_FLT100)
			ffstr_catfmt(dst, "%.2F", (double)i / 100);
		else if (sett->flags & CVTF_FLT10)
			ffstr_catfmt(dst, "%.1F", (double)i / 10);

	} else if (sett->flags & CVTF_MSEC) {
		int i = *(int*)src;
		if (i == SETT_EMPTY_INT)
			return 0;

		ffdatetime dt;
		fftime t;
		fftime_from_msec(&t, i);
		fftime_split1(&dt, &t);
		ffvec_grow(dst, 64, 1);
		dst->len = fftime_tostr1(&dt, dst->ptr, dst->cap, FFTIME_HMS_MSEC);

	} else {
		int i = *(int*)src;
		if (i == SETT_EMPTY_INT)
			return 0;

		ffstr_catfmt(dst, "%d", i);
	}
	return 0;
}

/** Setting's string value -> internal representation. */
int sett_fromstr(const struct cvt_set *st, void *obj, ffstr *data)
{
	void *p = (char*)obj + st->field_off;
	int *pint = p;
	int val;
	double d;

	if (st->flags & CVTF_STR) {
		char **pstr = p;
		ffmem_free(*pstr);
		if (data->len == 0) {
			*pstr = NULL;
			return 0;
		}
		*pstr = ffsz_dup(data->ptr);
		return 0;
	}

	if (data->len == 0) {
		*pint = SETT_EMPTY_INT;
		return 0;
	}

	if (st->flags & CVTF_MSEC) {
		ffdatetime dt;
		fftime t;
		if (data->len != fftime_fromstr1(&dt, data->ptr, data->len, FFTIME_HMS_MSEC_VAR))
			return -1;

		fftime_join1(&t, &dt);
		val = fftime_ms(&t);

	} else if (st->flags & CVTF_FLT) {
		if (data->len != ffs_tofloat(data->ptr, data->len, &d, 0))
			return -1;

		if (st->flags & CVTF_FLT10)
			val = d * 10;
		else if (st->flags & CVTF_FLT100)
			val = d * 100;

	} else {
		if (data->len != ffs_toint(data->ptr, data->len, &val, FFS_INT32))
			return -1;
	}

	*pint = val;
	return 0;
}

/** Update setting value */
static int sett_update(uint i, const char *newval)
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_t *cs = &w->conv_sets;
	ffstr s = FFSTR_INITZ(newval);
	int r = 0;

	switch (settings[i].id) {
	case SETT_CONVFMT:
		r = conf_format(NULL, cs, &s);
		break;

	case SETT_CONVCHAN:
		r = conf_channels(NULL, cs, &s);
		break;

	default:
		r = sett_fromstr(&settings[i], cs, &s);
	}
	return r;
}

static void conv_sett_tostr(uint i, ffvec *buf)
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_t *cs = &w->conv_sets;
	buf->len = 0;

	switch (i) {
	case SETT_CONVFMT:
		if (cs->format != SETT_EMPTY_INT)
			ffvec_addsz(buf, ffpcm_fmtstr(cs->format));
		break;

	case SETT_CONVCHAN:
		if (cs->channels != SETT_EMPTY_INT)
			ffvec_addsz(buf, ffpcm_channelstr(cs->channels));
		break;

	default:
		sett_tostr(cs, &settings[i], buf);
	}
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

static void track_props_set(fmed_track_info *ti)
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_t *cs = &w->conv_sets;

	if (cs->seek != SETT_EMPTY_INT)
		ti->audio.seek = cs->seek;
	if (cs->until != SETT_EMPTY_INT)
		ti->audio.until = cs->until;

	if (cs->format != SETT_EMPTY_INT)
		ti->audio.convfmt.format = cs->format;
	if (cs->conv_pcm_rate != SETT_EMPTY_INT)
		ti->audio.convfmt.channels = cs->conv_pcm_rate;
	if (cs->channels != SETT_EMPTY_INT)
		ti->audio.convfmt.sample_rate = cs->channels;

	if (cs->gain != SETT_EMPTY_INT)
		ti->audio.gain = cs->gain;

	if (cs->stream_copy != SETT_EMPTY_INT)
		ti->stream_copy = cs->stream_copy;
	if (cs->aac_quality != SETT_EMPTY_INT)
		ti->aac.quality = cs->aac_quality;
	if (cs->aac_bandwidth != SETT_EMPTY_INT)
		ti->aac.bandwidth = cs->aac_bandwidth;
	if (cs->flac_complevel != SETT_EMPTY_INT)
		ti->flac.compression = cs->flac_complevel;
	if (cs->mpg_quality != SETT_EMPTY_INT)
		ti->mpeg.quality = cs->mpg_quality;
	if (cs->opus_bitrate != SETT_EMPTY_INT)
		ti->opus.bitrate = cs->opus_bitrate;
	if (cs->opus_bandwidth != SETT_EMPTY_INT)
		ti->opus.bandwidth = cs->opus_bandwidth;
	if (cs->opus_frsize != SETT_EMPTY_INT)
		ti->opus.frame_size = cs->opus_frsize;
	if (cs->vorbis_quality != SETT_EMPTY_INT)
		ti->vorbis.quality = cs->vorbis_quality + 10;

	if (cs->overwrite != SETT_EMPTY_INT)
		ti->out_overwrite = cs->overwrite;
	if (cs->out_preserve_date != SETT_EMPTY_INT)
		ti->out_preserve_date = cs->out_preserve_date;
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

		conf_fin();

		output_show();

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
		for (i = 0;  i != FF_COUNT(settings);  i++) {
			if (settings[i].name == NULL)
				break;

			if ((settings[i].flags & CVTF_NEWGRP) && i != 0)
				grp_id++;
			ffui_view_setgroupid(&it, grp_id);

			ffui_view_settextz(&it, settings[i].name);
			ffui_view_append(&w->vsets, &it);

			conv_sett_tostr(i, &s);
			ffui_view_settextstr(&it, &s);
			ffui_view_set(&w->vsets, VSETS_VAL, &it);

			ffui_view_settextz(&it, settings[i].desc);
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
	for (i = 0;  i != FF_COUNT(settings);  i++) {
		if (settings[i].id == name)
			break;
	}

	ffui_view_setindex(&it, i);
	ffui_view_settext(&it, buf, r);
	ffui_view_set(&w->vsets, VSETS_VAL, &it);
	ffui_view_itemreset(&it);
}

/** Create new tracks for selected files.  Pass conversion settings to each track.  Start the first added track. */
static void gui_convert(void)
{
	struct gui_wconvert *w = gg->wconvert;
	cvt_sets_t *cs = &w->conv_sets;
	int i;
	fmed_que_entry e, *qent, *inp;
	ffstr fn = {};
	ffarr ar = {0};

	ffui_textstr(&w->eout, &fn);
	if (fn.len == 0 || 0 == wmain_list_n_selected()) {
		errlog(core, NULL, "gui", "convert: no files selected");
		goto end;
	}

	// update output file names list
	i = output_add(fn.ptr);
	if (i != -1)
		ffui_combx_rm(&w->eout, w->conv_sets.output.len - i - 1);
	ffui_combx_insz(&w->eout, 0, fn.ptr);
	ffui_combx_set(&w->eout, 0);

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

		ffmem_zero_obj(&e);
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
		if (cs->meta != NULL) {
			gg->qu->meta_set(qent, "meta", ffsz_len("meta"), cs->meta, ffsz_len(cs->meta), FMED_QUE_TRKDICT);
		}

		fmed_trk trkprops;
		gg->track->copy_info(&trkprops, NULL);
		trkprops.out_filename = fn.ptr;
		track_props_set(&trkprops);
		gg->qu->cmdv(FMED_QUE_SETTRACKPROPS, qent, &trkprops);
	}

	gui_showque(itab);

	if (ar.len != 0) {
		qent = *(void**)ar.ptr;
		gg->qu->cmdv(FMED_QUE_XPLAY, qent);
	}

end:
	ffstr_free(&fn);
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

static void conv_writeval(ffconfw *conf, const char *name, ffuint i)
{
	switch (i) {
	case SETT_OUT:
		ffconfw_addkeyz(conf, name);
		output_write(conf);
		return;
	}

	ffvec buf = {};
	conv_sett_tostr(i, &buf);
	if (buf.len != 0) {
		ffconfw_addkeyz(conf, name);
		ffconfw_addstr(conf, (ffstr*)&buf);
	}
	ffvec_free(&buf);
}

int wconvert_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wconvert *w = gg->wconvert;
	char name[64];
	uint name_off = FFS_LEN("gui.gui.convert.");
	ffsz_copyz(name, sizeof(name), "gui.gui.convert.");
	uint name_cap = sizeof(name) - name_off;

	if (w->wconf_used == NULL)
		w->wconf_used = ffmem_calloc(FF_COUNT(settings), 1);

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(settings);  i++) {
			if (!w->wconf_used[i]) {
				ffsz_copyz(name + name_off, name_cap, cvt_sets_conf[i].name);
				conv_writeval(conf, name, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(settings);  i++) {
		ffsz_copyz(name + name_off, name_cap, cvt_sets_conf[i].name);
		if (ffstr_matchz(line, name)) {
			conv_writeval(conf, name, i);
			w->wconf_used[i] = 1;
			return 1;
		}
	}
	return 0;
}

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
		w->vsets_edit_idx = i;
		}
		break;

	case CVT_SETS_EDITDONE: {
		uint i = w->vsets_edit_idx;
		w->vsets_edit_idx = 0;
		if (0 != sett_update(i, w->vsets.text))
			break;

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_settextz(&it, w->vsets.text);
		ffui_view_set(&w->vsets, VSETS_VAL, &it);
		}
		break;
	}
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
	ffmem_free(w->wconf_used);
	ffmem_free0(gg->wconvert);
}
