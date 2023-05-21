/** fmedia: command-line arguments
2021, Simon Zolin */

#include <util/cmdarg-scheme.h>

#define cl_errlog(fmt, ...)  errlog0("incorrect command-line: " fmt, ##__VA_ARGS__)

void arg_usage_short()
{
	const char *usage =
"Usage:\n"
"    fmedia [OPTIONS] [INPUT...]\n"
"Use '-h' option for complete help info.\n";
	ffstd_write(ffstdout, usage, ffsz_len(usage));
}

#define CMD_LAST  100
#define FFCMDARG_ERROR  1

static int arg_usage(ffcmdarg_scheme *as, void *obj)
{
	ffvec buf = {};
	char *fn = NULL, *name = NULL;
	int r = FFCMDARG_ERROR;

	// try language file, then use default file
	char lang[8];
	int n = ffenv_locale(lang, sizeof(lang), FFENV_LANGUAGE);
	ffstr loc = {};
	if (n > 0) {
		ffstr_set(&loc, lang, n);
		ffstr_splitby(&loc, '_', &loc, NULL);
	}
	int def_file = 0;
	if (loc.len != 0)
		name = ffsz_allocfmt("help_%S.txt", &loc);
	else {
		def_file = 1;
		name = ffsz_dup("help.txt");
	}
	fn = core->getpath(name, ffsz_len(name));

	for (;;) {
		if (0 != fffile_readwhole(fn, &buf, 64*1024)) {
			if (!def_file && fferr_nofile(fferr_last())) {
				ffmem_free(fn);
				fn = core->getpath("help.txt", ffsz_len("help.txt"));
				def_file = 1;
				continue;
			}
			goto done;
		}
		break;
	}

	ffstd_write(ffstdout, buf.ptr, buf.len);
	r = CMD_LAST;

done:
	if (r != CMD_LAST)
		syserrlog0("trying to read help.txt");
	ffvec_free(&buf);
	ffmem_free(fn);
	ffmem_free(name);
	return r;
}

static int arg_infile(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	char **fn;
	if (NULL == (fn = ffarr_pushgrowT(&cmd->in_files, 4, char*)))
		return FFCMDARG_ERROR;
	if (NULL == (*fn = ffsz_alcopystr(val)))
		return FFCMDARG_ERROR;
	return 0;
}

static void listdev(ffarr *buf, uint flags, uint flags2)
{
	fmed_adev_ent *ents = NULL;
	const fmed_modinfo *mod;
	const fmed_adev *adev = NULL;

	if (NULL == (mod = core->getmod2(flags2, NULL, 0))
		|| NULL == (adev = mod->m->iface("adev")))
		goto end;

	uint ndev = adev->list(&ents, flags);
	if ((int)ndev < 0)
		goto end;

	const char *title = (flags == FMED_ADEV_PLAYBACK) ? "Playback/Loopback" : "Capture";
	ffstr_catfmt(buf, "%s:\n", title);
	for (uint i = 0;  i != ndev;  i++) {
		ffstr def = {};
		if (ents[i].default_device)
			ffstr_setz(&def, " - Default");
		ffstr_catfmt(buf, "device #%u: %s%S\n", i + 1, ents[i].name, &def);

		const ffpcm *df = &ents[i].default_format;
		if (df->format != 0)
			ffstr_catfmt(buf, " Default Format: %u channel, %u Hz\n"
				, df->channels, df->sample_rate);
	}

end:
	if (ents != NULL)
		adev->listfree(ents);
}

static int arg_listdev()
{
	ffarr buf;
	ffarr_alloc(&buf, 1024);

	listdev(&buf, FMED_ADEV_PLAYBACK, FMED_MOD_INFO_ADEV_OUT);
	listdev(&buf, FMED_ADEV_CAPTURE, FMED_MOD_INFO_ADEV_IN);

	if (buf.len != 0)
		ffstd_write(ffstdout, buf.ptr, buf.len);
	ffarr_free(&buf);
	return CMD_LAST;
}

static int arg_format(ffcmdarg_scheme *as, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FFCMDARG_ERROR;
	conf->out_format = r;
	return 0;
}

static int arg_channels(ffcmdarg_scheme *as, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FFCMDARG_ERROR;
	conf->out_channels = r;
	return 0;
}

static int arg_out_copycmd(ffcmdarg_scheme *as, void *obj)
{
	fmed_cmd *cmd = obj;
	cmd->out_copy = FMED_OUTCP_CMD;
	return 0;
}

/** Read file line by line and add filenames as input arguments. */
static int arg_flist(ffcmdarg_scheme *as, void *obj, const char *fn)
{
	int r = FFCMDARG_ERROR;
	uint cnt = 0;
	ffarr buf = {0};
	ffstr line, d;

	dbglog(core, NULL, "core", "opening file %s", fn);

	if (0 != fffile_readwhole(fn, &buf, -1))
		goto done;
	ffstr_setstr(&d, &buf);
	while (d.len != 0) {
		ffstr_splitby(&d, '\n', &line, &d);
		ffstr_trimwhite(&line);
		if (line.len == 0)
			continue;
		if (0 != arg_infile(as, obj, &line))
			goto done;
		cnt++;
	}

	dbglog(core, NULL, "core", "added %u filenames from %s", cnt, fn);

	r = 0;

done:
	ffarr_free(&buf);
	return r;
}

// "WILDCARD[;WILDCARD]"
static int arg_finclude(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	int rc = FFCMDARG_ERROR;
	fmed_cmd *cmd = obj;
	ffstr *dst, s, wc;
	char *sz = ffsz_dupstr(val);
	ffarr a = {};
	ffstr_setz(&s, sz);
	while (s.len != 0) {
		ffstr_nextval3(&s, &wc, ';');
		if (NULL == (dst = ffarr_pushgrowT(&a, 4, ffstr)))
			goto end;
		*dst = wc;
	}

	if (ffsz_eq(as->arg->long_name, "include")) {
		ffarr_set(&cmd->include_files, a.ptr, a.len);
		cmd->include_files_data = sz;
	} else {
		ffarr_set(&cmd->exclude_files, a.ptr, a.len);
		cmd->exclude_files_data = sz;
	}
	sz = NULL;
	ffarr_null(&a);
	rc = 0;

end:
	ffmem_free(sz);
	ffarr_free(&a);
	return rc;
}

/* "DB[;TIME][;TIME]" */
static int arg_astoplev(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	ffstr db, time, mintime;
	ffdatetime dt;
	fftime t;
	ffstr_splitby(val, ';', &db, &time);
	ffstr_splitby(&time, ';', &time, &mintime);

	double f;
	if (db.len == 0 || db.len != ffs_tofloat(db.ptr, db.len, &f, 0))
		return FFCMDARG_ERROR;
	cmd->stop_level = f;

	if (time.len != 0) {
		if (time.len != fftime_fromstr1(&dt, time.ptr, time.len, FFTIME_HMS_MSEC_VAR))
			return FFCMDARG_ERROR;

		fftime_join1(&t, &dt);
		cmd->stop_level_time = fftime_ms(&t);
	}

	if (mintime.len != 0) {
		if (mintime.len != fftime_fromstr1(&dt, mintime.ptr, mintime.len, FFTIME_HMS_MSEC_VAR))
			return FFCMDARG_ERROR;

		fftime_join1(&t, &dt);
		cmd->stop_level_mintime = fftime_ms(&t);
	}

	return 0;
}

static int arg_seek(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	uint i;
	ffdatetime dt;
	fftime t;
	if (val->len != fftime_fromstr1(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFCMDARG_ERROR;

	fftime_join1(&t, &dt);
	i = fftime_ms(&t);

	if (!ffsz_cmp(as->arg->long_name, "seek"))
		cmd->seek_time = i;
	else if (!ffsz_cmp(as->arg->long_name, "until"))
		cmd->until_time = i;
	else
		cmd->prebuffer = i;
	return 0;
}

static int arg_until(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	if (ffstr_eqcz(val, "playback-end")) {
		cmd->until_plback_end = 1;
		return 0;
	}
	return arg_seek(as, obj, val);
}

static int arg_split(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	ffdatetime dt;
	fftime t;
	if (val->len != fftime_fromstr1(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFCMDARG_ERROR;

	fftime_join1(&t, &dt);
	cmd->split_time = fftime_ms(&t);
	return 0;
}

static int arg_install(ffcmdarg_scheme *as, void *obj)
{
#ifdef FF_WIN
	const fmed_modinfo *mi = core->insmod("gui.gui", NULL);
	if (mi != NULL) {
		uint sig = ffsz_eq(as->arg->long_name, "install") ? FMED_SIG_INSTALL : FMED_SIG_UNINSTALL;
		mi->m->sig(sig);
	}
#endif
	return CMD_LAST;
}

static int arg_input_chk(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdin"))
			core->props->stdin_busy = 1;
	}
	return 0;
}

static int arg_out_chk(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdout"))
			core->props->stdout_busy = 1;
	}
	return 0;
}

static int arg_debug(ffcmdarg_scheme *as, void *obj)
{
	core->loglev = FMED_LOG_DEBUG;
	return 0;
}

static int arg_auto_attenuate(ffcmdarg_scheme *as, void *obj, double val)
{
	fmed_cmd *cmd = obj;
	if (val > 0) {
		cl_errlog("must be <0", 0);
		return FFCMDARG_ERROR;
	}
	cmd->auto_attenuate = val;
	return 0;
}

static int arg_skipstr(ffcmdarg_scheme *as, void *obj, const ffstr *val)
{
	return 0;
}

#define O(member)  FF_OFF(fmed_cmd, member)
#define F(func)  (ffsize)func
#define TSTR  FFCMDARG_TSTR | FFCMDARG_FNOTEMPTY
#define TSTRZ  FFCMDARG_TSTRZ | FFCMDARG_FNOTEMPTY
#define TSWITCH  FFCMDARG_TSWITCH
#define TINT32  FFCMDARG_TINT32
#define TFLOAT32  FFCMDARG_TFLOAT32 | FFCMDARG_FSIGN

static const ffcmdarg_arg fmed_cmdline_args[] = {
	{ 0, "",	TSTR,	F(arg_infile) },

	//QUEUE
	{ 0, "repeat-all",	TSWITCH,	O(repeat_all) },
	{ 0, "random",	TSWITCH,	O(list_random) },
	{ 0, "track",	TSTRZ,	O(trackno) },

	//AUDIO DEVICES
	{ 0, "list-dev",	TSWITCH,	F(arg_listdev) },
	{ 0, "dev",	TINT32,	O(playdev_name) },
	{ 0, "dev-capture",	TINT32,	O(captdev_name) },
	{ 0, "dev-loopback",	TINT32,	O(lbdev_name) },
	{ 0, "playback-buffer",	FFCMDARG_TINT16,	O(play_buf_len) },

	//AUDIO FORMAT
	{ 0, "format",	TSTR,	F(arg_format) },
	{ 0, "rate",	TINT32,	O(out_rate) },
	{ 0, "channels",	TSTR,	F(arg_channels) },

	//INPUT
	{ 0, "record",	TSWITCH,	O(rec) },
	{ 0, "capture-buffer",	FFCMDARG_TINT16,	O(capture_buf_len) },
	{ 0, "mix",	TSWITCH,	O(mix) },
	{ 0, "flist",	TSTRZ | FFCMDARG_FMULTI,	F(arg_flist) },
	{ 0, "include",	TSTR,	F(arg_finclude) },
	{ 0, "exclude",	TSTR,	F(arg_finclude) },
	{ 's', "seek",	TSTR,	F(arg_seek) },
	{ 'u', "until",	TSTR,	F(arg_until) },
	{ 0, "prebuffer",	TSTR,	F(arg_seek) },
	{ 0, "start-dblevel",	TFLOAT32,	O(start_level) },
	{ 0, "stop-dblevel",	TSTR,	F(arg_astoplev) },
	{ 0, "fseek",	FFCMDARG_TINT64,	O(fseek) },
	{ 'i', "info",	TSWITCH,	O(info) },

	// TAGS
	{ 0, "tags",	TSWITCH,	O(tags) },
	{ 0, "meta",	FFCMDARG_TSTR,	O(meta) },
	{ 0, "meta-from-filename",	FFCMDARG_TSTR,	O(meta_from_filename) },
	{ 0, "edit-tags",	TSWITCH,	O(edittags) },

	//FILTERS
	{ 0, "volume",	FFCMDARG_TINT8,	O(volume) },
	{ 0, "gain",	TFLOAT32,	O(gain) },
	{ 0, "auto-attenuate",	TFLOAT32,	F(arg_auto_attenuate) },
	{ 0, "split",	TSTR,	F(arg_split) },
	{ 0, "dynanorm",	TSWITCH,	O(dynanorm) },
	{ 'P', "pcm-peaks",	TSWITCH,	O(pcm_peaks) },
	{ 0, "pcm-crc",	TSWITCH,	O(pcm_crc) },

	//ENCODING
	{ 0, "vorbis.quality",	TFLOAT32,	O(vorbis_qual) }, // obsolete
	{ 0, "opus.bitrate",	TINT32,	O(opus_brate) }, // obsolete
	{ 0, "vorbis-quality",	TFLOAT32,	O(vorbis_qual) },
	{ 0, "opus-bitrate",	TINT32,	O(opus_brate) },
	{ 0, "mpeg-quality",	FFCMDARG_TINT16,	O(mpeg_qual) },
	{ 0, "aac-quality",	TINT32,	O(aac_qual) },
	{ 0, "aac-profile",	TSTRZ,	O(aac_profile) },
	{ 0, "flac-compression",	FFCMDARG_TINT8,	O(flac_complevel) },
	{ 0, "stream-copy",	TSWITCH,	O(stream_copy) },

	//OUTPUT
	{ 'o', "out",	TSTRZ,	O(outfnz) },
	{ 'y', "overwrite",	TSWITCH,	O(overwrite) },
	{ 0, "out-copy",	TSWITCH,	O(out_copy) },
	{ 0, "out-copy-cmd",	TSWITCH,	F(arg_out_copycmd) },
	{ 0, "preserve-date",	TSWITCH,	O(preserve_date) },

	//OTHER OPTIONS
	{ 0, "background",	TSWITCH,	O(bground) },
#ifdef FF_WIN
	{ 0, "background-child",	TSWITCH,	O(bgchild) },
#endif
	{ 0, "globcmd",	TSTR,	O(globcmd) },
	{ 0, "globcmd.pipe-name",	TSTRZ,	O(globcmd_pipename) },
	{ 0, "http-ctl",	FFCMDARG_TSTRZ,	O(http_ctl_options) },
	{ 0, "conf",	FFCMDARG_TSTR,	F(arg_skipstr) },
	{ 0, "notui",	TSWITCH,	O(notui) },
	{ 0, "gui",	TSWITCH,	O(gui) },
	{ 0, "print-time",	TSWITCH,	O(print_time) },
	{ 'D', "debug",	TSWITCH,	F(arg_debug) },
	{ 'h', "help",	TSWITCH,	F(arg_usage) },
	{ 0, "cue-gaps",	FFCMDARG_TINT8,	O(cue_gaps) },
	{ 0, "parallel",	TSWITCH,	O(parallel) },
	{ 0, "playlist-heal",	FFCMDARG_TSTRZ,	O(playlist_heal) },

	//INSTALL
	{ 0, "install",	TSWITCH,	F(arg_install) },
	{ 0, "uninstall",	TSWITCH,	F(arg_install) },
	{},
};

static const ffcmdarg_arg fmed_cmdline_main_args[] = {
	{ 0, "",	TSTR,	F(arg_input_chk) },
	{ 'o', "out",	TSTR,	F(arg_out_chk) },
	{ 0, "conf",	TSTRZ,	O(conf_fn) },
	{ 0, "notui",	TSWITCH,	O(notui) },
	{ 0, "gui",	TSWITCH,	O(gui) },
	{ 'D', "debug",	TSWITCH,	F(arg_debug) },
	{ 'h', "help",	TSWITCH,	F(arg_usage) },
	{},
};

#undef O
#undef F

int cmdline_validate(fmed_cmd *cmd)
{
	if (cmd->out_copy != 0 && cmd->outfn.len == 0) {
		cl_errlog("--out-copy requires --out");
		return 1;
	}
	return 0;
}

/**
Return 0: success
 -1: success, exit
 other: fatal error */
static int fmed_cmdline(int argc, char **argv, uint main_only)
{
	int r;
	ffcmdarg a;
	ffcmdarg_init(&a, (const char**)argv, argc);

	ffcmdarg_scheme as;
	const ffcmdarg_arg *args = (main_only) ? fmed_cmdline_main_args : fmed_cmdline_args;
	uint flags = (main_only) ? FFCMDARG_SCF_SKIP_UNKNOWN : 0;
	ffcmdarg_scheme_init(&as, args, g->cmd, &a, flags);

	for (;;) {
		ffstr val;
		r = ffcmdarg_parse(&a, &val);
		if (r < 0)
			break;

		r = ffcmdarg_scheme_process(&as, r);
		if (r <= 0) {
			break;
		}
	}

	if (r == -CMD_LAST) {
		r = -1;
	} else if (r != 0) {
		const char *err = "bad value";
		if (r == -FFCMDARG_ESCHEME)
			err = as.errmsg;
		cl_errlog("near '%S': %s"
			, &a.val, err);
		r = 1;
	}

	if (g->cmd->outfnz != NULL)
		ffstr_setz(&g->cmd->outfn, g->cmd->outfnz);
	if (r == 0 && !main_only) {
		r = cmdline_validate(g->cmd);
	}
	return r;
}
