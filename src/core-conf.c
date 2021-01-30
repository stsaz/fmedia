/**
Copyright (c) 2019 Simon Zolin */

#include <core-priv.h>
#include <format-detector.h>
#include <FF/data/utf8.h>


enum {
	CONF_MBUF = 2 * 4096,
};


static int conf_mod(ffparser_schem *p, void *obj, ffstr *val);
static int conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int conf_output(ffparser_schem *p, void *obj, ffstr *val);
static int conf_input(ffparser_schem *p, void *obj, ffstr *val);
static int conf_recfmt(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int conf_inp_format(ffparser_schem *p, void *obj, ffstr *val);
static int conf_inp_channels(ffparser_schem *p, void *obj, ffstr *val);
static int conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int conf_ext_val(ffparser_schem *p, void *obj, ffstr *val);
static int conf_codepage(ffparser_schem *p, void *obj, ffstr *val);
static int conf_include(ffparser_schem *p, void *obj, ffstr *val);
static int conf_portable(ffparser_schem *p, void *obj, const int64 *val);

// enum FMED_INSTANCE_MODE
static const char *const im_enumstr[] = {
	"off", "add", "play", "clear_play"
};
static const ffpars_enumlist im_enum = { im_enumstr, FFCNT(im_enumstr), FFPARS_DSTOFF(fmed_config, instance_mode) };

static const ffpars_arg conf_args[] = {
	{ "workers",	FFPARS_TINT8, FFPARS_DSTOFF(fmed_config, workers) },
	{ "mod",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FMULTI, FFPARS_DST(&conf_mod) },
	{ "mod_conf",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&conf_modconf) },
	{ "output",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&conf_output) },
	{ "output_optional",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&conf_output) },
	{ "input",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&conf_input) },
	{ "record_format",	FFPARS_TOBJ, FFPARS_DST(&conf_recfmt) },
	{ "input_ext",	FFPARS_TOBJ, FFPARS_DST(&conf_ext) },
	{ "output_ext",	FFPARS_TOBJ, FFPARS_DST(&conf_ext) },
	{ "codepage",	FFPARS_TSTR, FFPARS_DST(&conf_codepage) },
	{ "instance_mode",	FFPARS_TENUM | FFPARS_F8BIT, FFPARS_DST(&im_enum) },
	{ "prevent_sleep",	FFPARS_TBOOL8, FFPARS_DSTOFF(fmed_config, prevent_sleep) },
	{ "include",	FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&conf_include) },
	{ "include_user",	FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&conf_include) },
	{ "portable_conf",	FFPARS_TBOOL8, FFPARS_DST(&conf_portable) },
};

static int confusr_mod(ffparser_schem *ps, void *obj, ffstr *val);

static const ffpars_arg confusr_args[] = {
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI | FFPARS_FLIST, FFPARS_DST(&confusr_mod) },
};

#define MODS_WIN_ONLY  "wasapi.", "direct-sound.", "#winsleep."
#define MODS_LINUX_ONLY  "alsa.", "pulse.", "jack.", "dbus."
#define MODS_BSD_ONLY  "oss."
#define MODS_MAC_ONLY  "coreaudio."

static const char *const mods_skip[] = {
#if defined FF_WIN
	MODS_LINUX_ONLY, MODS_BSD_ONLY, MODS_MAC_ONLY
#elif defined FF_LINUX
	MODS_WIN_ONLY, MODS_BSD_ONLY, MODS_MAC_ONLY
#elif defined FF_BSD
	MODS_WIN_ONLY, MODS_LINUX_ONLY, MODS_MAC_ONLY
#elif defined FF_APPLE
	MODS_WIN_ONLY, MODS_LINUX_ONLY, MODS_BSD_ONLY
#endif
};

/** Return 1 if the module is allowed to load. */
static int allowed_mod(const ffstr *name)
{
	if (ffstr_matchz(name, "gui.") && !core->props->gui)
		return 0;
	if (ffstr_matchz(name, "tui.") && (!core->props->tui || core->props->gui))
		return 0;

	const char *const *s;
	FFARRS_FOREACH(mods_skip, s) {
		if (ffstr_matchz(name, *s))
			return 0;
	}
	return 1;
}

static int conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	if (!allowed_mod(val))
		goto end;

	if (NULL == core_insmod_delayed(val->ptr, NULL))
		return FFPARS_ESYS;

end:
	ffstr_free(val);
	return 0;
}

static int conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	const ffstr *name = &p->vals[0];
	char *zname;
	fmed_config *conf = obj;

	if (!allowed_mod(name)) {
		ffpars_ctx_skip(ctx);
		return 0;
	}

	zname = ffsz_alcopy(name->ptr, name->len);
	if (zname == NULL)
		return FFPARS_ESYS;

	const fmed_modinfo *m;
	if (ffstr_matchz(name, "tui.")
		|| ffstr_matchz(name, "gui.")
		|| conf->conf_copy_mod != NULL) {
		// UI module must load immediately
		// Note: delayed modules loading from "include" config directive isn't supported
		m = core->insmod(zname, ctx);
	} else {
		m = core_insmod_delayed(zname, ctx);
		if (m != NULL && zname[0] != '#') {
			ffconf_ctxcopy_init(&conf->conf_copy, p);
			conf->conf_copy_mod = (void*)m;
		}
	}

	if (m == NULL) {
		ffmem_free(zname);
		return FFPARS_ESYS;
	}

	ffmem_free(zname);
	return 0;
}

static int conf_output(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val) || conf->output != NULL)
		return 0;

	const void *out;
	if (NULL == (out = core->getmod2(FMED_MOD_INFO, val->ptr, val->len))) {
		if (ffsz_eq(ffpars_argname(p), "output_optional"))
			return 0;
		return FFPARS_EBADVAL;
	}
	conf->output = out;
	return 0;
}

static int conf_input(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val))
		return 0;

	if (NULL == (conf->input = core->getmod2(FMED_MOD_INFO, val->ptr, val->len)))
		return FFPARS_EBADVAL;
	return 0;
}

static int conf_inp_format(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->inp_pcm.format = r;
	return 0;
}

static int conf_inp_channels(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->inp_pcm.channels = r;
	return 0;
}

static const ffpars_arg conf_input_args[] = {
	{ "format",	FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&conf_inp_format) },
	{ "channels",	FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&conf_inp_channels) },
	{ "rate",	FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(fmed_config, inp_pcm.sample_rate) }
};

static int conf_recfmt(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	ffpars_setargs(ctx, conf, conf_input_args, FFCNT(conf_input_args));
	return 0;
}

static int conf_ext_val(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	size_t n;
	inmap_item *it;
	ffstr3 *map = conf->inoutmap;
	ffconf *backend = ffpars_schem_backend(p);

	if (backend->type == FFCONF_TKEY) {
		const fmed_modinfo *mod = core_getmodinfo(val);
		if (mod == NULL) {
			return FFPARS_EBADVAL;
		}
		conf->inmap_curmod = mod;
		return 0;
	}

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffarr_grow(map, n, 64 | FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	it = (void*)(map->ptr + map->len);
	map->len += n;
	it->mod = conf->inmap_curmod;
	ffsz_fcopy(it->ext, val->ptr, val->len);
	return 0;
}

static const ffpars_arg conf_ext_args[] = {
	{ "*",	FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FLIST, FFPARS_DST(&conf_ext_val) }
};

static int conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	conf->inoutmap = &conf->inmap;
	if (!ffsz_cmp(p->curarg->name, "output_ext"))
		conf->inoutmap = &conf->outmap;
	ffpars_setargs(ctx, conf, conf_ext_args, FFCNT(conf_ext_args));
	return 0;
}

static int conf_codepage(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int cp = ffu_coding(val->ptr, val->len);
	if (cp == -1)
		return FFPARS_EBADVAL;
	conf->codepage = cp;
	return 0;
}

static int conf_portable(ffparser_schem *p, void *obj, const int64 *val)
{
	if (*val == 0)
		return 0;
	ffmem_free(core->props->user_path);
	if (NULL == (core->props->user_path = core->getpath(NULL, 0)))
		return FFPARS_ESYS;
	return 0;
}

static int conf_include(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int r = FFPARS_EBADVAL;
	char *fn = NULL;

	if (!ffsz_cmp(p->curarg->name, "include")) {
		if (NULL == (fn = core->getpath(val->ptr, val->len))) {
			r = FFPARS_ESYS;
			goto end;
		}

	} else {
		if (NULL == (fn = ffsz_alfmt("%s%S", core->props->user_path, val))) {
			r = FFPARS_ESYS;
			goto end;
		}
	}

	if (0 != core_conf_parse(conf, fn, CONF_F_USR | CONF_F_OPT))
		goto end;

	r = 0;
end:
	ffmem_safefree(fn);
	return r;
}

/** Process "so.modname" part from "so.modname.key value" */
static int confusr_mod(ffparser_schem *ps, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	const core_mod *mod;
	ffconf *pconf = ffpars_schem_backend(ps);

	if (conf->skip_line == pconf->line) {
		return 0;
	}

	if (pconf->type != FFCONF_TKEYCTX)
		return FFPARS_EUKNKEY;

	if (ffstr_eqcz(val, "core"))
		ffpars_setctx(ps, conf, conf_args, FFCNT(conf_args));

	else if (conf->usrconf_modname == NULL) {
		if (NULL == (conf->usrconf_modname = ffsz_alcopy(val->ptr, val->len)))
			return FFPARS_ESYS;

	} else {
		ffstr3 s = {0};
		if (0 == ffstr_catfmt(&s, "%s.%S", conf->usrconf_modname, val))
			return FFPARS_ESYS;

		if (!allowed_mod((ffstr*)&s)) {
			conf->skip_line = pconf->line;
			goto end;
		}

		if (NULL == (mod = core->getmod2(FMED_MOD_INFO, s.ptr, s.len))) {
			infolog0("user config: unknown module: %S", &s);
			conf->skip_line = pconf->line;
			goto end;
		}

		if (mod->conf_ctx.args == NULL) {
			infolog0("user config: module doesn't support configuration: %S", &s);
			conf->skip_line = pconf->line;
			goto end;
		}
		ffpars_setctx(ps, mod->conf_ctx.obj, mod->conf_ctx.args, mod->conf_ctx.nargs);

end:
		ffmem_free0(conf->usrconf_modname);
		ffarr_free(&s);
		return 0;
	}

	return 0;
}

int core_conf_parse(fmed_config *conf, const char *filename, uint flags)
{
	ffconf pconf;
	ffparser_schem ps;
	ffpars_ctx ctx = {0};
	int r = FFPARS_ESYS;
	ffstr s;
	char *buf = NULL;
	size_t n;
	fffd f = FF_BADFD;

	if (flags & CONF_F_USR) {
		ffpars_setargs(&ctx, conf, confusr_args, FFCNT(confusr_args));
	} else {
		ffpars_setargs(&ctx, conf, conf_args, FFCNT(conf_args));
	}

	ffconf_scheminit(&ps, &pconf, &ctx);
	if (FF_BADFD == (f = fffile_open(filename, O_RDONLY))) {
		if (fferr_nofile(fferr_last()) && (flags & CONF_F_OPT)) {
			r = 0;
			goto fail;
		}
		syserrlog("%s: %s", fffile_open_S, filename);
		goto fail;
	}

	dbglog(core, NULL, "core", "reading config file %s", filename);

	if (NULL == (buf = ffmem_alloc(CONF_MBUF))) {
		goto err;
	}

	for (;;) {
		n = fffile_read(f, buf, CONF_MBUF);
		if (n == (size_t)-1) {
			goto err;
		} else if (n == 0)
			break;
		ffstr_set(&s, buf, n);

		while (s.len != 0) {
			r = ffconf_parsestr(&pconf, &s);
			if (ffpars_iserr(r))
				goto err;

			if (conf->conf_copy_mod != NULL) {
				int r2 = ffconf_ctx_copy(&conf->conf_copy, &pconf);
				if (r2 < 0) {
					errlog0("parse config: %s: %u:%u: ffconf_ctx_copy()"
						, filename
						, pconf.line, pconf.ch);
					goto fail;
				} else if (r2 > 0) {
					core_mod *m = (void*)conf->conf_copy_mod;
					m->conf_data = ffconf_ctxcopy_acquire(&conf->conf_copy);
					m->have_conf = 1;
					conf->conf_copy_mod = NULL;
				}
				continue;
			}

			r = ffconf_schemrun(&ps);

			if (ffpars_iserr(r))
				goto err;
		}
	}

	r = ffconf_schemfin(&ps);

err:
	if (ffpars_iserr(r)) {
		const char *ser = ffpars_schemerrstr(&ps, r, NULL, 0);
		errlog(core, NULL, "core"
			, "parse config: %s: %u:%u: near \"%S\": \"%s\": %s"
			, filename
			, pconf.line, pconf.ch
			, &pconf.val, (ps.curarg != NULL) ? ps.curarg->name : ""
			, (r == FFPARS_ESYS) ? fferr_strp(fferr_last()) : ser);
		goto fail;
	}

	r = 0;

fail:
	conf->conf_copy_mod = NULL;
	ffconf_ctxcopy_destroy(&conf->conf_copy);
	ffconf_parseclose(&pconf);
	ffpars_schemfree(&ps);
	ffmem_safefree(buf);
	if (f != FF_BADFD)
		fffile_close(f);
	return r;
}
