/** fmedia: command-line arguments
2021, Simon Zolin */

#include <FF/data/psarg.h>

#ifdef FF_WIN
	#include <versionhelpers.h>
#endif

/** Get locale suffix. */
static const char* loc_sfx()
{
	uint ilang = 0;
	static const char langstr[][4] = {
		"", "" /*EN*/, "_RU", "_DE", "_FR", "_ES", "_ID",
	};
	uint lang = fflang_info(FFLANG_FLANG);
	dbglog(core, NULL, "core", "language:%xu", lang);
	switch (lang) {
	case FFLANG_ENG:
		ilang = 1; break;
	case FFLANG_RUS:
		ilang = 2; break;
	case FFLANG_GER:
		ilang = 3; break;
	case FFLANG_FRA:
		ilang = 4; break;
	case FFLANG_ESP:
		ilang = 5; break;
	case FFLANG_IND:
		ilang = 6; break;
	}

#ifdef FF_WIN
	if (ilang > 1 && !IsWindows7OrGreater()) // does printing non-latin characters work on old Windows?
		ilang = 1;
#endif

	return langstr[ilang];
}

#define FMED_CMDHELP_FILE_FMT  "help%s.txt"

static int arg_usage(void)
{
	ffarr buf = {0};
	ffssize n;
	char *fn = NULL, *name = NULL;
	fffd f = FF_BADFD;
	int r = FFPARS_ESYS;

	name = ffsz_alfmt(FMED_CMDHELP_FILE_FMT, loc_sfx());
	if (NULL == (fn = core->getpath(name, ffsz_len(name))))
		goto done;

	if (FF_BADFD == (f = fffile_open(fn, FFO_RDONLY | FFO_NOATIME)))
		goto done;

	if (NULL == ffarr_alloc(&buf, fffile_size(f)))
		goto done;

	n = fffile_read(f, buf.ptr, buf.cap);
	if (n <= 0)
		goto done;
	buf.len = n;

	ffstd_write(ffstdout, buf.ptr, buf.len);
	r = FFPARS_ELAST;

done:
	if (r != FFPARS_ELAST)
		syserrlog0("trying to read help.txt");
	ffmem_safefree(fn);
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffarr_free(&buf);
	ffmem_free(name);
	return r;
}

static int arg_infile(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	char **fn;
	if (NULL == (fn = ffarr_pushgrowT(&cmd->in_files, 4, char*)))
		return FFPARS_ESYS;
	if (NULL == (*fn = ffsz_alcopystr(val)))
		return FFPARS_ESYS;
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

static int arg_listdev(void)
{
	ffarr buf;
	ffarr_alloc(&buf, 1024);

	listdev(&buf, FMED_ADEV_PLAYBACK, FMED_MOD_INFO_ADEV_OUT);
	listdev(&buf, FMED_ADEV_CAPTURE, FMED_MOD_INFO_ADEV_IN);

	if (buf.len != 0)
		ffstd_write(ffstdout, buf.ptr, buf.len);
	ffarr_free(&buf);
	return FFPARS_ELAST;
}

static int arg_format(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->out_format = r;
	return 0;
}

static int arg_channels(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_cmd *conf = obj;
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->out_channels = r;
	return 0;
}

static const char* const outcp_str[] = { "all", "cmd" };

static int arg_out_copy(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	int r = FMED_OUTCP_ALL;
	if (val != NULL) {
		r = ffszarr_findsorted(outcp_str, FF_COUNT(outcp_str), val->ptr, val->len);
		if (r < 0)
			return FFPARS_EVALUNSUPP;
		r++;
	}
	cmd->out_copy = r;
	return 0;
}

/** Read file line by line and add filenames as input arguments. */
static int arg_flist(ffparser_schem *p, void *obj, const char *fn)
{
	int r = FFPARS_ESYS;
	uint cnt = 0;
	ffssize n;
	fffd f = FF_BADFD;
	ffarr buf = {0};
	ffstr line;
	const char *d, *end, *lf, *ln_end;

	dbglog(core, NULL, "core", "opening file %s", fn);

	if (FF_BADFD == (f = fffile_open(fn, FFO_RDONLY | FFO_NOATIME)))
		goto done;
	if (NULL == ffarr_alloc(&buf, fffile_size(f)))
		goto done;
	if (0 > (n = fffile_read(f, buf.ptr, buf.cap)))
		goto done;
	d = buf.ptr;
	end = buf.ptr + n;
	while (d != end) {
		lf = ffs_find(d, end - d, '\n');
		d = ffs_skipof(d, lf - d, " \t", 2);
		ln_end = ffs_rskipof(d, lf - d, " \t\r", 3);
		ffstr_set(&line, d, ln_end - d);
		if (lf != end)
			d = lf + 1;
		else
			d = lf;
		if (line.len == 0)
			continue;
		if (0 != arg_infile(p, obj, &line))
			goto done;
		cnt++;
	}

	dbglog(core, NULL, "core", "added %u filenames from %s", cnt, fn);

	r = 0;

done:
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	ffarr_free(&buf);
	return r;
}

// "WILDCARD[;WILDCARD]"
static int arg_finclude(ffparser_schem *p, void *obj, const ffstr *val)
{
	int rc = FFPARS_ESYS;
	fmed_cmd *cmd = obj;
	ffstr *dst, s = *val, wc;
	ffarr a = {};
	while (s.len != 0) {
		ffstr_nextval3(&s, &wc, ';');
		if (NULL == (dst = ffarr_pushgrowT(&a, 4, ffstr)))
			goto end;
		*dst = wc;
	}

	if (ffsz_eq(p->curarg->name, "include"))
		ffarr_set(&cmd->include_files, a.ptr, a.len);
	else
		ffarr_set(&cmd->exclude_files, a.ptr, a.len);
	ffarr_null(&a);
	rc = 0;

end:
	ffarr_free(&a);
	return rc;
}

/* "DB[;TIME][;TIME]" */
static int arg_astoplev(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	ffstr db, time, mintime;
	ffdtm dt;
	fftime t;
	ffs_split2by(val->ptr, val->len, ';', &db, &time);
	ffs_split2by(time.ptr, time.len, ';', &time, &mintime);

	double f;
	if (db.len == 0 || db.len != ffs_tofloat(db.ptr, db.len, &f, 0))
		return FFPARS_EBADVAL;
	cmd->stop_level = f;

	if (time.len != 0) {
		if (time.len != fftime_fromstr(&dt, time.ptr, time.len, FFTIME_HMS_MSEC_VAR))
			return FFPARS_EBADVAL;

		fftime_join(&t, &dt, FFTIME_TZNODATE);
		cmd->stop_level_time = fftime_ms(&t);
	}

	if (mintime.len != 0) {
		if (mintime.len != fftime_fromstr(&dt, mintime.ptr, mintime.len, FFTIME_HMS_MSEC_VAR))
			return FFPARS_EBADVAL;

		fftime_join(&t, &dt, FFTIME_TZNODATE);
		cmd->stop_level_mintime = fftime_ms(&t);
	}

	return 0;
}

static int arg_seek(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	uint i;
	ffdtm dt;
	fftime t;
	if (val->len != fftime_fromstr(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFPARS_EBADVAL;

	fftime_join(&t, &dt, FFTIME_TZNODATE);
	i = fftime_ms(&t);

	if (!ffsz_cmp(p->curarg->name, "seek"))
		cmd->seek_time = i;
	else if (!ffsz_cmp(p->curarg->name, "until"))
		cmd->until_time = i;
	else
		cmd->prebuffer = i;
	return 0;
}

static int arg_until(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	if (ffstr_eqcz(val, "playback-end")) {
		cmd->until_plback_end = 1;
		return 0;
	}
	return arg_seek(p, obj, val);
}

static int arg_split(ffparser_schem *p, void *obj, const ffstr *val)
{
	fmed_cmd *cmd = obj;
	ffdtm dt;
	fftime t;
	if (val->len != fftime_fromstr(&dt, val->ptr, val->len, FFTIME_HMS_MSEC_VAR))
		return FFPARS_EBADVAL;

	fftime_join(&t, &dt, FFTIME_TZNODATE);
	cmd->split_time = fftime_ms(&t);
	return 0;
}

static int arg_install(ffparser_schem *p, void *obj, const ffstr *val)
{
#ifdef FF_WIN
	const fmed_modinfo *mi = core->insmod("gui.gui", NULL);
	if (mi != NULL)
		mi->m->sig(!ffsz_cmp(p->curarg->name, "install") ? FMED_SIG_INSTALL : FMED_SIG_UNINSTALL);
#endif
	return FFPARS_ELAST;
}

static int arg_skip(ffparser_schem *p, void *obj, const ffstr *val)
{
	return 0;
}

static int arg_input_chk(ffparser_schem *p, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdin"))
			core->props->stdin_busy = 1;
	}
	return 0;
}

static int arg_out_chk(ffparser_schem *p, void *obj, const ffstr *val)
{
	ffstr fname, name;
	if (NULL == ffpath_split2(val->ptr, val->len, NULL, &fname)) {
		ffpath_splitname(fname.ptr, fname.len, &name, NULL);
		if (ffstr_eqcz(&name, "@stdout"))
			core->props->stdout_busy = 1;
	}
	return 0;
}

static int arg_debug(ffparser_schem *p, void *obj, const int64 *val)
{
	core->loglev = FMED_LOG_DEBUG;
	return 0;
}

static int arg_auto_attenuate(ffparser_schem *p, void *obj, const double *val)
{
	fmed_cmd *cmd = obj;
	if (*val > 0) {
		errlog0("must be <0", 0);
		return FFPARS_EBADVAL;
	}
	cmd->auto_attenuate = *val;
	return 0;
}

#define OFF(member)  FFPARS_DSTOFF(fmed_cmd, member)

static const ffpars_arg fmed_cmdline_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI,  FFPARS_DST(&arg_infile) },

	//QUEUE
	{ "repeat-all",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(repeat_all) },
	{ "random",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(list_random) },
	{ "track",	FFPARS_TCHARPTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  OFF(trackno) },

	//AUDIO DEVICES
	{ "list-dev",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&arg_listdev) },
	{ "dev",	FFPARS_TINT,  OFF(playdev_name) },
	{ "dev-capture",	FFPARS_TINT,  OFF(captdev_name) },
	{ "dev-loopback",	FFPARS_TINT,  OFF(lbdev_name) },

	//AUDIO FORMAT
	{ "format",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_format) },
	{ "rate",	FFPARS_TINT,  OFF(out_rate) },
	{ "channels",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_channels) },

	//INPUT
	{ "record",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(rec) },
	{ "capture-buffer",	FFPARS_TINT16,  OFF(capture_buf_len) },
	{ "mix",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(mix) },
	{ "flist",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&arg_flist) },
	{ "include",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY, FFPARS_DST(&arg_finclude) },
	{ "exclude",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY, FFPARS_DST(&arg_finclude) },
	{ "seek",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_seek) },
	{ "until",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_until) },
	{ "prebuffer",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_seek) },
	{ "start-dblevel",	FFPARS_TFLOAT | FFPARS_FSIGN,  OFF(start_level) },
	{ "stop-dblevel",	FFPARS_TSTR,  FFPARS_DST(&arg_astoplev) },
	{ "fseek",	FFPARS_TINT | FFPARS_F64BIT,  OFF(fseek) },
	{ "info",	FFPARS_SETVAL('i') | FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(info) },
	{ "tags",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(tags) },
	{ "meta",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FSTRZ,  OFF(meta) },

	//FILTERS
	{ "volume",	FFPARS_TINT8,  OFF(volume) },
	{ "gain",	FFPARS_TFLOAT | FFPARS_FSIGN,  OFF(gain) },
	{ "auto-attenuate",	FFPARS_TFLOAT | FFPARS_FSIGN,  FFPARS_DST(&arg_auto_attenuate) },
	{ "split",	FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&arg_split) },
	{ "dynanorm",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(dynanorm) },
	{ "pcm-peaks",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(pcm_peaks) },
	{ "pcm-crc",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(pcm_crc) },

	//ENCODING
	{ "vorbis.quality",	FFPARS_TFLOAT | FFPARS_FSIGN,  OFF(vorbis_qual) },
	{ "opus.bitrate",	FFPARS_TINT,  OFF(opus_brate) },
	{ "mpeg-quality",	FFPARS_TINT | FFPARS_F16BIT,  OFF(mpeg_qual) },
	{ "aac-quality",	FFPARS_TINT,  OFF(aac_qual) },
	{ "aac-profile",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  OFF(aac_profile) },
	{ "flac-compression",	FFPARS_TINT8,  OFF(flac_complevel) },
	{ "stream-copy",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(stream_copy) },

	//OUTPUT
	{ "out",	FFPARS_SETVAL('o') | FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  OFF(outfn) },
	{ "overwrite",	FFPARS_SETVAL('y') | FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(overwrite) },
	{ "out-copy",	FFPARS_TSTR | FFPARS_FALONE,  FFPARS_DST(&arg_out_copy) },
	{ "preserve-date",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(preserve_date) },

	//OTHER OPTIONS
	{ "background",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(bground) },
#ifdef FF_WIN
	{ "background-child",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(bgchild) },
#endif
	{ "globcmd",	FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  OFF(globcmd) },
	{ "globcmd.pipe-name",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  OFF(globcmd_pipename) },
	{ "conf",	FFPARS_TSTR,  OFF(dummy) },
	{ "notui",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(notui) },
	{ "gui",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(gui) },
	{ "print-time",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(print_time) },
	{ "debug",	FFPARS_TBOOL8 | FFPARS_FALONE,  FFPARS_DST(&arg_debug) },
	{ "help",	FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&arg_usage) },
	{ "cue-gaps",	FFPARS_TINT8,  OFF(cue_gaps) },
	{ "parallel",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(parallel) },

	//INSTALL
	{ "install",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&arg_install) },
	{ "uninstall",	FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&arg_install) },
};

static const ffpars_arg fmed_cmdline_main_args[] = {
	{ "",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&arg_input_chk) },
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI,  FFPARS_DST(&arg_skip) },
	{ "out",	FFPARS_SETVAL('o') | FFPARS_TSTR,  FFPARS_DST(&arg_out_chk) },
	{ "conf",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FNOTEMPTY,  OFF(conf_fn) },
	{ "notui",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(notui) },
	{ "gui",	FFPARS_TBOOL8 | FFPARS_FALONE,  OFF(gui) },
	{ "debug",	FFPARS_TBOOL8 | FFPARS_FALONE,  FFPARS_DST(&arg_debug) },
	{ "help",	FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&arg_usage) },
};

#undef OFF

static int fmed_cmdline(int argc, char **argv, uint main_only)
{
	ffparser_schem ps;
	ffpsarg_parser p;
	ffpars_ctx ctx = {0};
	int r = 0;
	int ret = 1;
	const char *arg;
	ffpsarg a;

	ffpsarg_init(&a, (void*)argv, argc);

	if (main_only)
		ffpars_setargs(&ctx, g->cmd, fmed_cmdline_main_args, FF_COUNT(fmed_cmdline_main_args));
	else
		ffpars_setargs(&ctx, g->cmd, fmed_cmdline_args, FF_COUNT(fmed_cmdline_args));

	if (0 != ffpsarg_scheminit(&ps, &p, &ctx)) {
		errlog0("cmd line parser", NULL);
		return 1;
	}

	ffpsarg_next(&a); //skip argv[0]

	arg = ffpsarg_next(&a);
	while (arg != NULL) {
		int n = 0;
		r = ffpsarg_parse(&p, arg, &n);
		if (n != 0)
			arg = ffpsarg_next(&a);

		r = ffpsarg_schemrun(&ps);

		if (r == FFPARS_ELAST) {
			ret = -1;
			goto fail;
		}

		if (ffpars_iserr(r))
			break;
	}

	if (!ffpars_iserr(r))
		r = ffpsarg_schemfin(&ps);

	if (ffpars_iserr(r)) {
		errlog0("cmd line parser: near \"%S\": %s"
			, &p.val, (r == FFPARS_ESYS) ? fferr_strp(fferr_last()) : ffpars_errstr(r));
		goto fail;
	}

	ret = 0;

fail:
	ffpsarg_destroy(&a);
	ffpars_schemfree(&ps);
	ffpsarg_parseclose(&p);
	return ret;
}
