/**
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>
#include <gui/gui.h>
#include <FF/time.h>
#include <FFOS/process.h>


enum {
	VSETS_NAME,
	VSETS_VAL,
	VSETS_DESC,
};

static void gui_rec_init(void);
static void gui_rec_action(ffui_wnd *wnd, int id);
static void gui_rec_browse(void);


void wrec_init()
{
	gg->wrec.wrec.hide_on_close = 1;
	gg->wrec.wrec.on_action = &gui_rec_action;
	gg->wrec.vsets.edit_id = CVT_SETS_EDITDONE;
}

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

void gui_rec_init(void)
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

void rec_setdev(int idev)
{
        gui_rec_init();

        uint i;
        for (i = 0;  i != FFCNT(rec_sets);  i++) {
                if (rec_sets[i].settname == SETT_DEV_REC)
                        break;
        }

        ffui_viewitem it = {};
        char buf[64];
        uint n = ffs_fromint(idev, buf, sizeof(buf), 0);

        ffui_view_setindex(&it, i);
        ffui_view_settext(&it, buf, n);
        ffui_view_set(&gg->wrec.vsets, VSETS_VAL, &it);
}
