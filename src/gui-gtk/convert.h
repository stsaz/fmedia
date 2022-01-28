/** fmedia: gui-gtk: convert files
2021, Simon Zolin */

struct gui_wconvert {
	ffui_wnd wnd;
	ffui_menu mmconv;
	ffui_label lfn, lsets;
	ffui_edit eout;
	ffui_btn boutbrowse;
	ffui_view vconfig;
	ffuint itab; // 1:first

	ffuint64 in_seek, in_until;
	ffuint filt_aformat, filt_srate, filt_channels;
	int filt_gain;

	ffuint enc_datacopy;
	ffuint enc_aac_qual;
	float enc_vorbis_qual;
	ffuint enc_opus_brate;
	int enc_mpeg_qual;

	ffuint out_overwrite;
	ffuint out_preservedate;
	char *output;

	ffbyte wconf_flags[8];
	uint init :1;
};

const ffui_ldr_ctl wconvert_ctls[] = {
	FFUI_LDR_CTL(struct gui_wconvert, wnd),
	FFUI_LDR_CTL(struct gui_wconvert, mmconv),
	FFUI_LDR_CTL(struct gui_wconvert, lfn),
	FFUI_LDR_CTL(struct gui_wconvert, lsets),
	FFUI_LDR_CTL(struct gui_wconvert, eout),
	FFUI_LDR_CTL(struct gui_wconvert, boutbrowse),
	FFUI_LDR_CTL(struct gui_wconvert, vconfig),
	FFUI_LDR_CTL_END
};

#define CONV_ENC_VORBIS_QUAL_NULL (255)
#define CONV_ENC_MPEG_QUAL_NULL (-1)

// CONFIG

static int conf_conv_sets_output(fmed_conf *fc, void *obj, char *val)
{
	struct gui_wconvert *c = obj;
	ffmem_free(c->output);
	c->output = ffsz_dup(val);
	return 0;
}
static int conf_any(fmed_conf *fc, void *obj, const ffstr *val)
{
	return 0;
}
static const fmed_conf_arg conf_conv[] = {
	{ "output",	FMC_STRZ_LIST, FMC_F(conf_conv_sets_output) },

	{ "vorbis_quality",	FMC_FLOAT32S, FMC_O(struct gui_wconvert, enc_vorbis_qual) },
	{ "opus_bitrate",	FMC_INT32, FMC_O(struct gui_wconvert, enc_opus_brate) },
	{ "mpeg_quality",	FMC_INT32, FMC_O(struct gui_wconvert, enc_mpeg_qual) },
	{ "aac_quality",	FMC_INT32, FMC_O(struct gui_wconvert, enc_aac_qual) },
	{ "data_copy",	FMC_INT32, FMC_O(struct gui_wconvert, enc_datacopy) },

	{ "*",	FMC_STR_MULTI, FMC_F(conf_any) },
	{}
};
int conf_convert(fmed_conf *fc, void *obj)
{
	fmed_conf_addnewctx(fc, gg->wconvert, conf_conv);
	return 0;
}

static void conv_writeval(ffconfw *conf, ffuint i)
{
	struct gui_wconvert *c = gg->wconvert;
	switch (i) {
	case 0:
		ffconfw_addint(conf, c->enc_aac_qual);
		break;
	case 1:
		ffconfw_addfloat(conf, c->enc_vorbis_qual, 2);
		break;
	case 2:
		ffconfw_addint(conf, c->enc_opus_brate);
		break;
	case 3:
		ffconfw_addint(conf, c->enc_mpeg_qual);
		break;
	case 4:
		ffconfw_addstrz(conf, c->output);
		break;
	case 5:
		ffconfw_addint(conf, c->enc_datacopy);
		break;
	}
}

int wconvert_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wconvert *c = gg->wconvert;
	static const char setts[][33] = {
		"gui.gui.convert.aac_quality",
		"gui.gui.convert.vorbis_quality",
		"gui.gui.convert.opus_bitrate",
		"gui.gui.convert.mpeg_quality",
		"gui.gui.convert.output",
		"gui.gui.convert.data_copy",
	};

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
			if (!c->wconf_flags[i]) {
				ffconfw_addkeyz(conf, setts[i]);
				conv_writeval(conf, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
		if (ffstr_matchz(line, setts[i])) {
			ffconfw_addkeyz(conf, setts[i]);
			conv_writeval(conf, i);
			c->wconf_flags[i] = 1;
			return 1;
		}
	}
	return 0;
}


enum {
	PROP_IN_SEEK = 1,
	PROP_IN_UNTIL,
	PROP_FILT_AFORMAT,
	PROP_FILT_SRATE,
	PROP_FILT_CHAN,
	PROP_FILT_GAIN,
	PROP_ENC_DATACOPY,
	PROP_ENC_AAC_QUAL,
	PROP_ENC_VORBIS_QUAL,
	PROP_ENC_OPUS_BRATE,
	PROP_ENC_MPEG_QUAL,
	PROP_OUT_OVERWRITE,
	PROP_OUT_PRESERVEDATE,
};

struct conv_prop_ent {
	const char *name;
	uint id;
};

static struct conv_prop_ent conv_props[] = {
	{ "Input:", 0 },
	{ "  Seek ([m:]s[.ms])",	PROP_IN_SEEK },
	{ "  Until ([m:]s[.ms])",	PROP_IN_UNTIL },

	{ "Filters:", 0 },
	{ "  Audio Format\n  (int8 | int16 | int24 | int32 | float32)",	PROP_FILT_AFORMAT },
	{ "  Sample Rate (Hz)",	PROP_FILT_SRATE },
	{ "  Channels",	PROP_FILT_CHAN },
	{ "  Gain (dB)",	PROP_FILT_GAIN },

	{ "Encoder:", 0 },
	{ "  .mp4/.mp3/.ogg: Data Copy (0 | 1)",	PROP_ENC_DATACOPY },
	{ "  .mp4: AAC Quality\n  (VBR-quality:1..5 | CBR-bitrate:8..800)",	PROP_ENC_AAC_QUAL },
	{ "  .ogg: Vorbis Quality\n  (-1.0 .. 10.0)",	PROP_ENC_VORBIS_QUAL },
	{ "  .opus: Opus Bitrate\n  (6..510)",	PROP_ENC_OPUS_BRATE },
	{ "  .mp3: MPEG-L3 Quality\n  (VBR-quality:9..0 | CBR-bitrate:64..320",	PROP_ENC_MPEG_QUAL },

	{ "Output:", 0 },
	{ "  Overwrite File (0 | 1)",	PROP_OUT_OVERWRITE },
	{ "  Preserve Date&Time (0 | 1)",	PROP_OUT_PRESERVEDATE },
};

/** Display conversion properties */
static void conv_disp(ffui_view *v)
{
	struct ffui_view_disp *disp = &v->disp;
	const struct conv_prop_ent *c = &conv_props[disp->idx];

	ffstr val;
	val.len = -1;
	char *zval = NULL;
	ffuint64 n;
	const char *sz;

	switch (disp->sub) {
	case 0:
		ffstr_setz(&val, c->name);
		break;

	case 1:
		switch (c->id) {
		case PROP_IN_SEEK:
		case PROP_IN_UNTIL:
			if (c->id == PROP_IN_SEEK)
				n = gg->wconvert->in_seek;
			else
				n = gg->wconvert->in_until;

			zval = ffsz_allocfmt("%U:%U.%U", n/60000, (n/1000)%60, n%1000);
			break;

		case PROP_FILT_AFORMAT:
			if (gg->wconvert->filt_aformat != 0) {
				sz = ffpcm_fmtstr(gg->wconvert->filt_aformat);
				ffstr_setz(&val, sz);
			}
			break;

		case PROP_FILT_SRATE:
			if (gg->wconvert->filt_srate != 0)
				zval = ffsz_allocfmt("%u", gg->wconvert->filt_srate);
			break;

		case PROP_FILT_CHAN:
			if (gg->wconvert->filt_channels != 0)
				zval = ffsz_allocfmt("%u", gg->wconvert->filt_channels);
			break;

		case PROP_FILT_GAIN:
			zval = ffsz_allocfmt("%d", gg->wconvert->filt_gain/100);
			break;

		case PROP_ENC_DATACOPY:
			sz = (gg->wconvert->enc_datacopy) ? "1" : "0";
			ffstr_setz(&val, sz);
			break;

		case PROP_ENC_AAC_QUAL:
			if (gg->wconvert->enc_aac_qual != 0)
				zval = ffsz_allocfmt("%u", gg->wconvert->enc_aac_qual);
			break;

		case PROP_ENC_VORBIS_QUAL:
			if (gg->wconvert->enc_vorbis_qual != CONV_ENC_VORBIS_QUAL_NULL)
				zval = ffsz_allocfmt("%.1f", gg->wconvert->enc_vorbis_qual);
			break;

		case PROP_ENC_OPUS_BRATE:
			if (gg->wconvert->enc_opus_brate != 0)
				zval = ffsz_allocfmt("%d", gg->wconvert->enc_opus_brate);
			break;

		case PROP_ENC_MPEG_QUAL:
			if (gg->wconvert->enc_mpeg_qual != CONV_ENC_MPEG_QUAL_NULL)
				zval = ffsz_allocfmt("%d", gg->wconvert->enc_mpeg_qual);
			break;

		case PROP_OUT_OVERWRITE:
			sz = (gg->wconvert->out_overwrite) ? "1" : "0";
			ffstr_setz(&val, sz);
			break;

		case PROP_OUT_PRESERVEDATE:
			sz = (gg->wconvert->out_preservedate) ? "1" : "0";
			ffstr_setz(&val, sz);
			break;
		}
	}

	if (zval != NULL)
		ffstr_setz(&val, zval);

	if (val.len != (ffsize)-1)
		disp->text.len = ffs_append(disp->text.ptr, 0, disp->text.len, val.ptr, val.len);

	ffmem_free(zval);
}

/** Modify conversion properties after edit */
static void conv_edit(ffui_view *v)
{
	const struct conv_prop_ent *c = &conv_props[v->edited.idx];
	ffstr text = FFSTR_INITZ(v->edited.new_text);
	int i;
	double fl;
	int k = 0;
	ffdatetime dt = {};
	fftime t;

	switch (c->id) {
	case PROP_IN_SEEK:
	case PROP_IN_UNTIL:
		if (text.len == fftime_fromstr1(&dt, text.ptr, text.len, FFTIME_HMS_MSEC_VAR)) {
			fftime_join1(&t, &dt);
			if (c->id == PROP_IN_SEEK)
				gg->wconvert->in_seek = fftime_to_msec(&t);
			else
				gg->wconvert->in_until = fftime_to_msec(&t);
			k = 1;
		}
		break;

	case PROP_FILT_AFORMAT:
		i = ffpcm_fmt(text.ptr, text.len);
		if (i < 0)
			i = 0;
		gg->wconvert->filt_aformat = i;
		k = 1;
		break;

	case PROP_FILT_SRATE:
		if (!ffstr_to_uint32(&text, &i))
			i = 0;
		gg->wconvert->filt_srate = i;
		k = 1;
		break;

	case PROP_FILT_CHAN:
		if (!ffstr_to_uint32(&text, &i))
			i = 0;
		if (i <= 8) {
			gg->wconvert->filt_channels = i;
			k = 1;
		}
		break;

	case PROP_FILT_GAIN:
		if (text.len == 0) {
			gg->wconvert->filt_gain = 0;
			k = 1;
		} else if (ffstr_to_int32(&text, &i)) {
			gg->wconvert->filt_gain = i*100;
			k = 1;
		}
		break;

	case PROP_ENC_DATACOPY:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->enc_datacopy = !!i;
			k = 1;
		}
		break;

	case PROP_ENC_AAC_QUAL:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->enc_aac_qual = i;
			k = 1;
		}
		break;

	case PROP_ENC_VORBIS_QUAL:
		if (ffstr_to_float(&text, &fl)) {
			gg->wconvert->enc_vorbis_qual = fl;
			k = 1;
		}
		break;

	case PROP_ENC_OPUS_BRATE:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->enc_opus_brate = i;
			k = 1;
		}
		break;

	case PROP_ENC_MPEG_QUAL:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->enc_mpeg_qual = i;
			k = 1;
		}
		break;

	case PROP_OUT_OVERWRITE:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->out_overwrite = !!i;
			k = 1;
		}
		break;

	case PROP_OUT_PRESERVEDATE:
		if (ffstr_to_uint32(&text, &i)) {
			gg->wconvert->out_preservedate = !!i;
			k = 1;
		}
		break;
	}

	if (k)
		ffui_view_setdata(v, gg->wconvert->vconfig.edited.idx, 0);
}

static void convert(void *param);

static void wconvert_action(ffui_wnd *wnd, int id)
{
	switch (id) {

	case A_CONVERT:
		corecmd_addfunc(convert, NULL);
		break;

	case A_CONV_MOVE_UNTIL:
		wconv_setdata(id, 0);
		break;

	case A_CONVOUTBROWSE: {
		char *fn;
		ffstr name = {};
		ffui_edit_textstr(&gg->wconvert->eout, &name);
		if (NULL != (fn = ffui_dlg_save(&gg->dlg, &gg->wconvert->wnd, name.ptr, name.len)))
			ffui_edit_settextz(&gg->wconvert->eout, fn);
		ffstr_free(&name);
		break;
	}

	case A_CONV_EDIT:
		conv_edit(&gg->wconvert->vconfig);
		break;

	case A_CONV_DISP:
		conv_disp(&gg->wconvert->vconfig);
		break;
	}
}

void wconvert_init()
{
	struct gui_wconvert *c = ffmem_new(struct gui_wconvert);
	c->wnd.hide_on_close = 1;
	c->wnd.on_action = &wconvert_action;
	c->vconfig.dispinfo_id = A_CONV_DISP;
	c->vconfig.edit_id = A_CONV_EDIT;

	c->enc_vorbis_qual = CONV_ENC_VORBIS_QUAL_NULL;
	c->enc_mpeg_qual = CONV_ENC_MPEG_QUAL_NULL;
	c->out_preservedate = 1;

	gg->wconvert = c;
}

void wconv_destroy()
{
	struct gui_wconvert *c = gg->wconvert;
	ffmem_free(c->output);
	ffmem_free0(gg->wconvert);
}

void wconv_show(uint show)
{
	struct gui_wconvert *w = gg->wconvert;

	if (!show) {
		ffui_show(&w->wnd, 0);
		return;
	}

	if (!w->init) {
		w->init = 1;
		ffui_edit_settextz(&w->eout, w->output);
	}

	ffui_view_setdata(&w->vconfig, 0, FF_COUNT(conv_props));
	ffui_show(&w->wnd, 1);
	ffui_wnd_present(&w->wnd);
}

void wconv_setdata(int id, uint pos)
{
	switch (id) {
	case A_CONV_SET_SEEK:
		gg->wconvert->in_seek = pos*1000; break;
	case A_CONV_SET_UNTIL:
		gg->wconvert->in_until = pos*1000; break;

	case A_CONV_MOVE_UNTIL:
		gg->wconvert->in_seek = gg->wconvert->in_until;
		gg->wconvert->in_until = 0;
		break;

	default:
		return;
	}
	ffui_view_setdata(&gg->wconvert->vconfig, 0, FF_COUNT(conv_props));
}

/** Add 1 item to the conversion queue. */
static fmed_que_entry* convert1(fmed_que_entry *input, const ffstr *fn, fmed_trk *trkinfo)
{
	struct gui_wconvert *c = gg->wconvert;
	fmed_que_entry e = {}, *qent;
	e.url = input->url;
	e.from = input->from;
	e.to = input->to;
	if (NULL == (qent = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, c->itab-1, &e)))
		return NULL;
	gg->qu->meta_set(qent, FFSTR("output"), fn->ptr, fn->len, FMED_QUE_TRKDICT);

	// copy meta from source
	ffstr sname, *sval;
	for (ffsize n = 0;  NULL != (sval = gg->qu->meta(input, n, &sname, FMED_QUE_NO_TMETA));  n++) {
		if (sval == FMED_QUE_SKIP)
			continue;
		gg->qu->meta_set(qent, sname.ptr, sname.len, sval->ptr, sval->len, 0);
	}

	gg->qu->cmdv(FMED_QUE_SETTRACKPROPS, qent, trkinfo);
	return qent;
}

/** Set current conversion properties on a track */
static void trkinfo_set(fmed_trk *ti)
{
	struct gui_wconvert *c = gg->wconvert;

	if (c->in_seek != 0)
		ti->audio.seek = c->in_seek;
	if (c->in_until != 0)
		ti->audio.until = c->in_until;

	if (c->filt_aformat != 0)
		ti->audio.convfmt.format = c->filt_aformat;
	if (c->filt_srate != 0)
		ti->audio.convfmt.sample_rate = c->filt_srate;
	if (c->filt_channels != 0)
		ti->audio.convfmt.channels = c->filt_channels;
	if (c->filt_gain != 0)
		ti->audio.gain = c->filt_gain;

	if (c->enc_datacopy != 0)
		ti->stream_copy = 1;
	if (c->enc_aac_qual != 0)
		ti->aac.quality = c->enc_aac_qual;
	if (c->enc_vorbis_qual != CONV_ENC_VORBIS_QUAL_NULL)
		ti->vorbis.quality = (c->enc_vorbis_qual+1) * 10;
	if (c->enc_opus_brate != 0)
		ti->opus.bitrate = c->enc_opus_brate;
	if (c->enc_mpeg_qual != 0)
		ti->mpeg.quality = c->enc_mpeg_qual;

	if (c->out_overwrite != 0)
		ti->out_overwrite = 1;
	if (c->out_preservedate != 0)
		ti->out_preserve_date = 1;
}

/** Begin conversion of all selected files using the current settings */
static void convert(void *param)
{
	struct gui_wconvert *c = gg->wconvert;
	ffstr fn = {};
	ffui_send_edit_textstr(&c->eout, &fn);
	if (fn.len == 0) {
		errlog("convert: output file name is empty");
		goto done;
	}

	ffmem_free(c->output);
	c->output = ffsz_alcopystr(&fn);

	fmed_trk trkinfo;
	gg->track->copy_info(&trkinfo, NULL);
	trkinfo_set(&trkinfo);

	// create "Convert" tab
	int curtab = wmain_tab_active();
	if (curtab < 0)
		goto done;
	c->itab = wmain_tab_new(TAB_CONVERT) + 1;
	gg->qu->cmdv(FMED_QUE_NEW, FMED_QUE_NORND);

	fmed_que_entry *first = NULL;
	int i;
	ffarr4 *sel = wmain_list_getsel();
	while (-1 != (i = ffui_view_selnext(NULL, sel))) {
		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(curtab, i);
		ent = convert1(ent, &fn, &trkinfo);
		if (first == NULL)
			first = ent;
	}
	ffui_view_sel_free(sel);

	wmain_list_select(c->itab-1);
	gui_list_sel(c->itab-1);

	if (first != NULL)
		gg->qu->cmdv(FMED_QUE_XPLAY, first);

done:
	ffstr_free(&fn);
}
