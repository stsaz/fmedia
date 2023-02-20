/** fmedia: GUI-winapi: Record dialog
Copyright (c) 2020 Simon Zolin */

#include <gui-winapi/gui.h>
#include <util/path.h>
#include <FFOS/process.h>


typedef struct rec_sets_t {
	uint init :1;

	int lpbk_devno;
	int devno;

	int format;
	int sample_rate;
	int channels;

	int gain;
	float gain_f;
	int use_dynanorm;
	int until;
	float until_f;

	int aac_quality;
	int flac_complevel;
	int mpg_quality;
	int opus_bitrate;
	int vorbis_quality;
	float vorbis_quality_f;

	char *output;
} rec_sets_t;

struct gui_wrec {
	ffui_wnd wrec;
	ffui_menu mmrec;
	ffui_label lfn, lsets;
	ffui_edit eout;
	ffui_btn boutbrowse;
	ffui_view vsets;
	ffui_paned pnout;

	rec_sets_t rec_sets;
	ffbyte *wconf_used;
	uint vsets_edit_idx;
	int wrec_init :1;
};

const ffui_ldr_ctl wrec_ctls[] = {
	FFUI_LDR_CTL(struct gui_wrec, wrec),
	FFUI_LDR_CTL(struct gui_wrec, mmrec),
	FFUI_LDR_CTL(struct gui_wrec, lfn),
	FFUI_LDR_CTL(struct gui_wrec, lsets),
	FFUI_LDR_CTL(struct gui_wrec, eout),
	FFUI_LDR_CTL(struct gui_wrec, boutbrowse),
	FFUI_LDR_CTL(struct gui_wrec, vsets),
	FFUI_LDR_CTL(struct gui_wrec, pnout),
	FFUI_LDR_CTL_END
};

enum {
	VSETS_NAME,
	VSETS_VAL,
	VSETS_DESC,
};

static void gui_rec_init(void);
static void gui_rec_browse(void);

/**
"int16" -> FFPCM_16LE; "" -> default */
static int conf_format(fmed_conf *fc, rec_sets_t *rs, ffstr *s)
{
	int r = ffpcm_fmt(s->ptr, s->len);
	if (r < 0) {
		if (s->len != 0)
			return FMC_EBADVAL;
		r = SETT_EMPTY_INT;
	}

	rs->format = r;
	return 0;
}

static int conf_channels(fmed_conf *fc, rec_sets_t *rs, ffstr *s)
{
	int r = ffpcm_channels(s->ptr, s->len);
	if (r < 0) {
		if (s->len != 0)
			return FMC_EBADVAL;
		r = SETT_EMPTY_INT;
	}

	rs->channels = r;
	return 0;
}

static int conf_until(fmed_conf *fc, rec_sets_t *rs, ffstr *s)
{
	ffdatetime dt;
	fftime t;
	if (s->len != fftime_fromstr1(&dt, s->ptr, s->len, FFTIME_HMS_MSEC_VAR))
		return FMC_EBADVAL;
	fftime_join1(&t, &dt);

	rs->until = fftime_to_msec(&t);
	return 0;
}

static int conf_fin()
{
	struct gui_wrec *w = gg->wrec;
	rec_sets_t *rs = &w->rec_sets;

	if (rs->gain_f != 0)
		rs->gain = rs->gain_f * 100;

	if (rs->vorbis_quality_f != -255)
		rs->vorbis_quality = rs->vorbis_quality_f * 10;

	return 0;
}

static const char* const rec_grps[] = {
	"Input",
	"Audio Format",
	"Audio Filters",
	"Codec",
};

enum {
	SETT_DEV_REC,
	SETT_DEV_LP,

	SETT_FMT,
	SETT_RATE,
	SETT_CHAN,

	SETT_GAIN,
	SETT_DYNANORM,
	SETT_UNTIL,

	SETT_AAC_QUAL,
	SETT_FLAC_COMP,
	SETT_MPEG_QUAL,
	SETT_OPUS_BRATE,
	SETT_VORB_QUAL,

	SETT_OUT,
};

#define O(a) FF_OFF(rec_sets_t, a)
static const struct cvt_set settings[] = {
	{ SETT_DEV_REC,	O(devno),	"Capture Device Number", "0: default", CVTF_NEWGRP },
	{ SETT_DEV_LP,	O(lpbk_devno),	"Loopback Device Number (record from playback)", "empty: disabled;  0: default device", 0 },

	{ SETT_FMT,	O(format),	"Audio Format", "int8 | int16 | int24 | int32 | float32", CVTF_NEWGRP },
	{ SETT_RATE,	O(sample_rate),	"Sample Rate (Hz)", "", 0 },
	{ SETT_CHAN,	O(channels),	"Channels", "2 (stereo) | 1 (mono) | left | right", 0 },

	{ SETT_GAIN,	O(gain),	"Gain (dB)", "", CVTF_FLT | CVTF_FLT100 | CVTF_NEWGRP },
	{ SETT_DYNANORM,	O(use_dynanorm),	"Enable Dynamic Audio Normalizer", "0 or 1", 0 },
	{ SETT_UNTIL,	O(until),	"Stop after", "[MM:]SS[.MSC]", CVTF_MSEC },

	{ SETT_AAC_QUAL,	O(aac_quality),	"AAC Quality", "VBR quality: 1..5 or CBR bitrate: 8..800", CVTF_NEWGRP },
	{ SETT_FLAC_COMP,	O(flac_complevel),	"FLAC Compression", "0..8", 0 },
	{ SETT_MPEG_QUAL,	O(mpg_quality),	"MPEG1-L3 Quality", "VBR quality: 9..0 or CBR bitrate: 64..320", 0 },
	{ SETT_OPUS_BRATE,	O(opus_bitrate),	"Opus Bitrate", "6..510", 0 },
	{ SETT_VORB_QUAL,	O(vorbis_quality),	"Vorbis Quality", "-1.0 .. 10.0", CVTF_FLT | CVTF_FLT10 },

	{ SETT_OUT,	O(output),	NULL, NULL, CVTF_STR },
};

// conf -> gui
static const fmed_conf_arg rec_sets_conf[] = {
	{ "capture_device",	FMC_INT32, O(devno) },
	{ "loopback_device",	FMC_INT32, O(lpbk_devno) },

	{ "format",	FMC_STR, FMC_F(conf_format) },
	{ "sample_rate",	FMC_INT32, O(sample_rate) },
	{ "channels",	FMC_STR, FMC_F(conf_channels) },

	{ "gain",	FMC_FLOAT32S, O(gain_f) },
	{ "dynanorm",	FMC_INT32, O(use_dynanorm) },
	{ "until",	FMC_STR, FMC_F(conf_until) },

	{ "aac_quality",	FMC_INT32, O(aac_quality) },
	{ "flac_complevel",	FMC_INT32, O(flac_complevel) },
	{ "mpeg_quality",	FMC_INT32, O(mpg_quality) },
	{ "opus_bitrate",	FMC_INT32, O(opus_bitrate) },
	{ "vorbis_quality",	FMC_FLOAT32S, O(vorbis_quality_f) },

	{ "output",	FMC_STRZ, O(output) },
	{}
};
#undef O

void rec_sets_init(rec_sets_t *sets)
{
	sets->init = 1;

	sets->devno = 0;
	sets->lpbk_devno = SETT_EMPTY_INT;

	sets->format = sets->sample_rate = sets->channels = SETT_EMPTY_INT;
	sets->gain_f = 0.0;
	sets->use_dynanorm = 0;
	sets->until = SETT_EMPTY_INT;

	sets->aac_quality = SETT_EMPTY_INT;
	sets->flac_complevel = SETT_EMPTY_INT;
	sets->mpg_quality = SETT_EMPTY_INT;
	sets->opus_bitrate = SETT_EMPTY_INT;
	sets->vorbis_quality = SETT_EMPTY_INT;
	sets->vorbis_quality_f = -255;
}

static void rec_sets_destroy(rec_sets_t *sets)
{
	ffmem_safefree0(sets->output);
}

int gui_conf_rec(fmed_conf *fc, void *obj)
{
	struct gui_wrec *w = gg->wrec;
	w->rec_sets.output = ffsz_dup("%APPDATA%\\fmedia\\rec-$date-$time.flac");
	fmed_conf_addnewctx(fc, &w->rec_sets, rec_sets_conf);
	return 0;
}

/** Update setting value */
static int sett_update(uint i, const char *newval)
{
	struct gui_wrec *w = gg->wrec;
	rec_sets_t *rs = &w->rec_sets;
	ffstr s = FFSTR_INITZ(newval);
	int r = 0;

	switch (i) {
	case SETT_FMT:
		r = conf_format(NULL, rs, &s);
		break;

	case SETT_CHAN:
		r = conf_channels(NULL, rs, &s);
		break;

	default:
		r = sett_fromstr(&settings[i], rs, &s);
		break;
	}
	return r;
}

static void rec_sett_tostr(uint i, ffvec *buf)
{
	struct gui_wrec *w = gg->wrec;
	rec_sets_t *rs = &w->rec_sets;
	buf->len = 0;
	switch (i) {
	case SETT_FMT:
		if (rs->format != SETT_EMPTY_INT)
			ffvec_addsz(buf, ffpcm_fmtstr(rs->format));
		break;

	case SETT_CHAN:
		if (rs->channels != SETT_EMPTY_INT)
			ffvec_addsz(buf, ffpcm_channelstr(rs->channels));
		break;

	default:
		sett_tostr(rs, &settings[i], buf);
	}
}

/** settings -> fmedia track */
static void track_props_set(void *trk, fmed_track_info *ti)
{
	struct gui_wrec *w = gg->wrec;
	rec_sets_t *rs = &w->rec_sets;

	if (rs->devno != SETT_EMPTY_INT)
		gg->track->setval(trk, "capture_device", rs->devno);
	if (rs->lpbk_devno != SETT_EMPTY_INT)
		gg->track->setval(trk, "loopback_device", rs->lpbk_devno);

	if (rs->format != SETT_EMPTY_INT)
		ti->audio.fmt.format = rs->format;
	if (rs->sample_rate != SETT_EMPTY_INT)
		ti->audio.fmt.sample_rate = rs->sample_rate;
	if (rs->channels != SETT_EMPTY_INT)
		ti->audio.fmt.channels = rs->channels;

	if (rs->gain != SETT_EMPTY_INT)
		ti->audio.gain = rs->gain;
	if (rs->use_dynanorm != SETT_EMPTY_INT)
		ti->use_dynanorm = rs->use_dynanorm;
	if (rs->until != SETT_EMPTY_INT)
		ti->audio.until = rs->until;

	if (rs->aac_quality != SETT_EMPTY_INT)
		ti->aac.quality = rs->aac_quality;
	if (rs->flac_complevel != SETT_EMPTY_INT)
		ti->flac.compression = rs->flac_complevel;
	if (rs->mpg_quality != SETT_EMPTY_INT)
		ti->mpeg.quality = rs->mpg_quality;
	if (rs->opus_bitrate != SETT_EMPTY_INT)
		ti->opus.bitrate = rs->opus_bitrate;
	if (rs->vorbis_quality != SETT_EMPTY_INT)
		ti->vorbis.quality = rs->vorbis_quality + 10;
}

void gui_rec_init(void)
{
	struct gui_wrec *w = gg->wrec;
	if (w->wrec_init)
		return;

	if (!w->rec_sets.init)
		rec_sets_init(&w->rec_sets);

	conf_fin();

	if (ffui_textlen(&w->eout) == 0)
		ffui_settextz(&w->eout, w->rec_sets.output);

	ffui_view_showgroups(&w->vsets, 1);
	const char *const *grp;
	int grp_id = 0;
	ffui_viewgrp vg;
	FFARRS_FOREACH(rec_grps, grp) {
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

		rec_sett_tostr(i, &s);
		ffui_view_settextstr(&it, &s);
		ffui_view_set(&w->vsets, VSETS_VAL, &it);

		ffui_view_settextz(&it, settings[i].desc);
		ffui_view_set(&w->vsets, VSETS_DESC, &it);
	}

	ffarr_free(&s);
	w->wrec_init = 1;
}

void wrec_show(uint show)
{
	struct gui_wrec *w = gg->wrec;

	if (!show) {
		ffui_show(&w->wrec, 0);
		return;
	}

	gui_rec_init();
	ffui_show(&w->wrec, 1);
	ffui_wnd_setfront(&w->wrec);
}

static const struct cmd rec_cmds[] = {
	{ REC,	F1 | CMD_FCORE,	&gui_rec },
	{ OUTBROWSE,	F0,	&gui_rec_browse },
};

static void gui_rec_browse(void)
{
	struct gui_wrec *w = gg->wrec;
	const char *fn;

	ffui_dlg_nfilter(&gg->dlg, DLG_FILT_OUTPUT);
	if (NULL == (fn = ffui_dlg_save(&gg->dlg, &w->wrec, NULL, 0)))
		return;

	ffui_settextz(&w->eout, fn);
}

static void gui_rec_action(ffui_wnd *wnd, int id)
{
	struct gui_wrec *w = gg->wrec;
	const struct cmd *cmd = getcmd(id, rec_cmds, FFCNT(rec_cmds));
	if (cmd != NULL) {
		if (cmd->flags & CMD_FCORE)
			gui_corecmd_add(cmd, NULL);
		else
			gui_runcmd(cmd, NULL);
		return;
	}

	switch (id) {
	case REC_OUT_LOSEFOCUS: {
		ffstr text = ffui_edit_text(&w->eout);
		sett_update(SETT_OUT, text.ptr);
		ffstr_free(&text);
		break;
	}

	case REC_SETT_EDIT: {
		int i, isub;
		ffui_point pt;
		ffui_cur_pos(&pt);
		if (-1 == (i = ffui_view_hittest(&w->vsets, &pt, &isub))
			|| isub != VSETS_VAL)
			return;
		ffui_view_edit(&w->vsets, i, VSETS_VAL);
		w->vsets_edit_idx = i;
		break;
	}

	case REC_SETT_EDIT_DONE: {
		uint i = w->vsets_edit_idx;
		w->vsets_edit_idx = 0;
		if (0 != sett_update(i, w->vsets.text))
			break;

		ffui_viewitem it;
		ffui_view_iteminit(&it);
		ffui_view_setindex(&it, i);
		ffui_view_settextz(&it, w->vsets.text);
		ffui_view_set(&w->vsets, VSETS_VAL, &it);
		break;
	}
	}
}

int gui_rec_addsetts(void *trk)
{
	struct gui_wrec *w = gg->wrec;

	if (w->wrec_init) {
		// update Output setting value, because lose-focus event isn't reliable
		ffstr text = ffui_edit_text(&w->eout);
		sett_update(SETT_OUT, text.ptr);
		ffstr_free(&text);
	}

	char *exp;
	if (NULL == (exp = core->env_expand(NULL, 0, w->rec_sets.output)))
		return -1;
	fmed_track_info *ti = gg->track->conf(trk);
	ti->out_filename = exp;
	exp = NULL;

	fmed_trk *trkconf = gg->track->conf(trk);
	track_props_set(trk, trkconf);
	ti->audio.convfmt = ti->audio.fmt;
	return 0;
}

void rec_setdev(int idev)
{
	struct gui_wrec *w = gg->wrec;
	gui_rec_init();

	uint i;
	for (i = 0;  i != FF_COUNT(settings);  i++) {
		if (settings[i].id == SETT_DEV_REC)
			break;
	}

	ffui_viewitem it = {};
	char buf[64];
	uint n = ffs_fromint(idev, buf, sizeof(buf), 0);

	ffui_view_setindex(&it, i);
	ffui_view_settext(&it, buf, n);
	ffui_view_set(&w->vsets, VSETS_VAL, &it);
}

static void rec_conf_writeval(ffconfw *conf, const char *name, ffuint i)
{
	ffvec buf = {};
	rec_sett_tostr(i, &buf);
	if (buf.len != 0) {
		ffconfw_addkeyz(conf, name);
		ffconfw_addstr(conf, (ffstr*)&buf);
	}
	ffvec_free(&buf);
}

int wrec_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wrec *w = gg->wrec;
	char name[64];
	uint name_off = FFS_LEN("gui.gui.record.");
	ffsz_copyz(name, sizeof(name), "gui.gui.record.");
	uint name_cap = sizeof(name) - name_off;

	if (w->wconf_used == NULL)
		w->wconf_used = ffmem_calloc(FF_COUNT(settings), 1);

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(settings);  i++) {
			if (!w->wconf_used[i]) {
				ffsz_copyz(name + name_off, name_cap, rec_sets_conf[i].name);
				rec_conf_writeval(conf, name, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(settings);  i++) {
		ffsz_copyz(name + name_off, name_cap, rec_sets_conf[i].name);
		if (ffstr_matchz(line, name)) {
			rec_conf_writeval(conf, name, i);
			w->wconf_used[i] = 1;
			return 1;
		}
	}
	return 0;
}

void wrec_showrecdir()
{
	struct gui_wrec *w = gg->wrec;
	char *p, *exp;
	ffstr dir;
	ffpath_split2(w->rec_sets.output, ffsz_len(w->rec_sets.output), &dir, NULL);
	if (NULL == (p = ffsz_alcopy(dir.ptr, dir.len)))
		return;
	if (NULL == (exp = core->env_expand(NULL, 0, p)))
		goto done;
	ffui_openfolder((const char *const *)&exp, 0);

done:
	ffmem_safefree(p);
	ffmem_safefree(exp);
}

void wrec_init()
{
	struct gui_wrec *w = ffmem_new(struct gui_wrec);
	gg->wrec = w;
	w->wrec.hide_on_close = 1;
	w->wrec.on_action = &gui_rec_action;
	w->eout.focus_lose_id = REC_OUT_LOSEFOCUS;
	w->vsets.edit_id = REC_SETT_EDIT_DONE;
	rec_sets_init(&w->rec_sets);
}

void wrec_destroy()
{
	struct gui_wrec *w = gg->wrec;
	rec_sets_destroy(&w->rec_sets);
	ffmem_free(w->wconf_used);
	ffmem_free0(gg->wrec);
}
