/**
Copyright (c) 2019 Simon Zolin */

// CONFIG

static int conf_conv_sets_output(ffparser_schem *ps, void *obj, char *val)
{
	struct conv_sets *c = obj;
	ffmem_free(c->output);
	c->output = val;
	return 0;
}
static int conf_any(ffparser_schem *p, void *obj, const ffstr *val)
{
	return 0;
}
static const ffpars_arg conf_conv[] = {
	{ "output",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FLIST, FFPARS_DST(&conf_conv_sets_output) },

	{ "*",	FFPARS_TSTR | FFPARS_FMULTI, FFPARS_DST(&conf_any) },
};
int conf_convert(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &gg->conv_sets, conf_conv, FFCNT(conf_conv));
	return 0;
}


static void wconvert_action(ffui_wnd *wnd, int id)
{
	switch (id) {

	case A_CONVERT:
		corecmd_add(A_CONVERT, NULL);
		break;

	case A_CONVOUTBROWSE: {
		char *fn;
		ffstr name;
		ffui_edit_textstr(&gg->wconvert.eout, &name);
		if (NULL != (fn = ffui_dlg_save(&gg->dlg, &gg->wconvert.wconvert, name.ptr, name.len)))
			ffui_edit_settextz(&gg->wconvert.eout, fn);
		ffstr_free(&name);
		break;
	}
	}
}

void wconvert_init()
{
	gg->wconvert.wconvert.hide_on_close = 1;
	gg->wconvert.wconvert.on_action = &wconvert_action;
}

void wconv_destroy()
{
	ffmem_free0(gg->conv_sets.output);
}

void wconv_show()
{
	if (!gg->conv_sets.init) {
		gg->conv_sets.init = 1;
		ffui_edit_settextz(&gg->wconvert.eout, gg->conv_sets.output);
	}
	ffui_show(&gg->wconvert.wconvert, 1);
}

/** Add 1 item to the conversion queue. */
static fmed_que_entry* convert1(fmed_que_entry *input, const ffstr *fn)
{
	fmed_que_entry e = {}, *qent;
	e.url = input->url;
	e.from = input->from;
	e.to = input->to;
	if (NULL == (qent = (void*)gg->qu->fmed_queue_add(FMED_QUE_NO_ONCHANGE, -1, &e)))
		return NULL;
	gg->qu->meta_set(qent, FFSTR("output"), fn->ptr, fn->len, FMED_QUE_TRKDICT);
	return qent;
}

/** Begin conversion of all selected files using the current settings. */
void convert()
{
	ffstr fn = {};
	ffui_edit_textstr(&gg->wconvert.eout, &fn);
	if (fn.len == 0) {
		errlog("convert: output file name is empty");
		goto done;
	}

	ffmem_free(gg->conv_sets.output);
	gg->conv_sets.output = ffsz_alcopystr(&fn);

	fmed_que_entry *first = NULL;
	int i;
	ffarr4 *sel = (void*)ffui_send_view_getsel(&gg->wmain.vlist);
	while (-1 != (i = ffui_view_selnext(&gg->wmain.vlist, sel))) {
		fmed_que_entry *ent = (fmed_que_entry*)gg->qu->fmed_queue_item(-1, i);
		ent = convert1(ent, &fn);
		if (first == NULL)
			first = ent;
		break;
	}
	ffui_view_sel_free(sel);

	if (first != NULL)
		gg->qu->cmdv(FMED_QUE_PLAY, first);

done:
	ffstr_free(&fn);
}
