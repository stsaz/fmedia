/**
Copyright (c) 2019 Simon Zolin */

#include <core/core-priv.h>
#include <util/conf2-ltconf.h>

void usrconf_read(ffconf_scheme *sc, ffstr key, ffstr val);

int conf_init(fmed_config *conf)
{
	conf->codepage = FFUNICODE_WIN1252;
	return 0;
}

void conf_destroy(fmed_config *conf)
{
	ffmap_free(&conf->in_ext_map);
	ffmap_free(&conf->out_ext_map);
	ffvec_free(&conf->inmap);
	ffvec_free(&conf->outmap);
}

enum {
	CONF_DELAYED = 100,
};

// enum FMED_INSTANCE_MODE
static const char *const im_enumstr[] = {
	"off", "add", "play", "clear_play"
};

static int conf_instance_mode(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	const char *const *it;
	FFARRS_FOREACH(im_enumstr, it) {
		if (ffstr_eqz(val, *it)) {
			conf->instance_mode = it - im_enumstr;
			return 0;
		}
	}
	return FMC_EBADVAL;
}

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

static int conf_mod(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	if (!allowed_mod(val))
		return 0;

	if (NULL == core_insmod_delayed(*val))
		return FMC_ESYS;
	return 0;
}

static int conf_modconf(fmed_conf *fc, fmed_config *conf)
{
	const ffstr *name = ffconf_scheme_objval(fc);

	if (!allowed_mod(name)) {
		fmed_conf_skipctx(fc);
		return 0;
	}

	int delayed = 0;
	const fmed_modinfo *m;
	if (name->ptr[0] == '#'
		|| ffstr_matchz(name, "tui.")
		|| ffstr_matchz(name, "gui.")
		|| conf->conf_copy_mod != NULL) {
		// UI module must load immediately
		// Note: delayed modules loading from "include" config directive isn't supported
		m = core_insmod(*name, fc);
	} else {
		m = core_insmod_delayed(*name);
		if (m != NULL && name->ptr[0] != '#') {
			ffconf_ctxcopy_init(&conf->conf_copy);
			conf->conf_copy_mod = (void*)m;
			delayed = 1;
		}
	}

	if (m == NULL) {
		return FMC_ESYS;
	}

	((core_mod*)m)->have_conf = 1;
	return !delayed ? 0 : CONF_DELAYED;
}

static int conf_output(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	if (!allowed_mod(val) || conf->output != NULL)
		return 0;

	const void *out;
	if (NULL == (out = core_getmodinfo(*val))) {
		if (ffsz_eq(fc->arg->name, "output_optional"))
			return 0;
		return FMC_EBADVAL;
	}
	conf->output = out;
	return 0;
}

static int conf_input(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	if (!allowed_mod(val))
		return 0;

	if (NULL == (conf->input = core_getmodinfo(*val)))
		return FMC_EBADVAL;
	return 0;
}

static int conf_inp_format(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FMC_EBADVAL;
	conf->inp_pcm.format = r;
	return 0;
}

static int conf_inp_channels(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FMC_EBADVAL;
	conf->inp_pcm.channels = r;
	return 0;
}

static const fmed_conf_arg conf_input_args[] = {
	{ "format",	FMC_STRNE, FMC_F(conf_inp_format) },
	{ "channels",	FMC_STRNE, FMC_F(conf_inp_channels) },
	{ "rate",	FMC_INT32NZ, FMC_O(fmed_config, inp_pcm.sample_rate) },
	{}
};

static int conf_recfmt(fmed_conf *fc, fmed_config *conf)
{
	ffconf_scheme_addctx(fc, conf_input_args, conf);
	return 0;
}

static int conf_ext_val(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	size_t n;
	inmap_item *it;
	ffvec *map = (conf->use_inmap) ? &conf->inmap : &conf->outmap;

	ffstr *keyname = ffconf_scheme_keyname(fc);
	const fmed_modinfo *mod = core_getmodinfo(*keyname);
	if (mod == NULL)
		return FMC_EBADVAL;

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffvec_grow(map, n, 1))
		return FMC_ESYS;
	it = (void*)ffslice_end(map, 1);
	map->len += n;
	it->mod = mod;
	ffsz_copyn(it->ext, -1, val->ptr, val->len);
	return 0;
}

static const fmed_conf_arg conf_ext_args[] = {
	{ "*",	FMC_STRNE | FFCONF_FLIST | FFCONF_FMULTI, FMC_F(conf_ext_val) },
	{}
};

static int conf_ext(fmed_conf *fc, fmed_config *conf)
{
	conf->use_inmap = 1;
	if (!ffsz_cmp(fc->arg->name, "output_ext"))
		conf->use_inmap = 0;
	ffconf_scheme_addctx(fc, conf_ext_args, conf);
	return 0;
}

static int ffu_coding(const char *data, size_t len)
{
	static const char *const codestr[] = {
		"win866", // FFUNICODE_WIN866
		"win1251", // FFUNICODE_WIN1251
		"win1252", // FFUNICODE_WIN1252
	};
	int r = ffszarr_ifindsorted(codestr, FFCNT(codestr), data, len);
	if (r < 0)
		return -1;
	return _FFUNICODE_CP_BEGIN + r;
}

static int conf_codepage(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	int cp = ffu_coding(val->ptr, val->len);
	if (cp == -1)
		return FMC_EBADVAL;
	conf->codepage = cp;
	return 0;
}

static int conf_portable(fmed_conf *fc, fmed_config *conf, int64 val)
{
	if (val == 0)
		return 0;
	ffmem_free(core->props->user_path);
	if (NULL == (core->props->user_path = core->getpath(NULL, 0)))
		return FMC_ESYS;
	return 0;
}

static int conf_include(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	int r = FMC_EBADVAL;
	char *fn = NULL;

	if (!ffsz_cmp(fc->arg->name, "include")) {
		if (NULL == (fn = core->getpath(val->ptr, val->len))) {
			r = FMC_ESYS;
			goto end;
		}

	} else {
		if (NULL == (fn = ffsz_alfmt("%s%S", core->props->user_path, val))) {
			r = FMC_ESYS;
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

static const fmed_conf_arg conf_args[] = {
	{ "workers",	FMC_INT8, FMC_O(fmed_config, workers) },
	{ "mod",	FMC_STRNE | FFCONF_FMULTI, FMC_F(conf_mod) },
	{ "mod_conf",	FMC_OBJ | FFCONF_FNOTEMPTY | FFCONF_FMULTI, FMC_F(conf_modconf) },
	{ "output",	FMC_STRNE | FFCONF_FMULTI, FMC_F(conf_output) },
	{ "output_optional",	FMC_STRNE | FFCONF_FMULTI, FMC_F(conf_output) },
	{ "input",	FMC_STRNE | FFCONF_FMULTI, FMC_F(conf_input) },
	{ "record_format",	FMC_OBJ, FMC_F(conf_recfmt) },
	{ "input_ext",	FMC_OBJ, FMC_F(conf_ext) },
	{ "output_ext",	FMC_OBJ, FMC_F(conf_ext) },
	{ "codepage",	FMC_STR, FMC_F(conf_codepage) },
	{ "instance_mode",	FMC_STRNE, FMC_F(conf_instance_mode) },
	{ "prevent_sleep",	FMC_BOOL8, FMC_O(fmed_config, prevent_sleep) },
	{ "include",	FMC_STRNE, FMC_F(conf_include) },
	{ "include_user",	FMC_STRNE, FMC_F(conf_include) },
	{ "portable_conf",	FMC_BOOL8, FMC_F(conf_portable) },
	{}
};

/** Process "[so.]modname[.key].key value" with a module configuration context */
static int confusr_mod(fmed_conf *fc, fmed_config *conf, ffstr *val)
{
	const ffstr *keyname = ffconf_scheme_keyname(fc);
	ffstr modname, key;
	if (ffstr_splitby(keyname, '.', &modname, &key) < 0) {
		infolog0("user config: bad key %S", keyname);
		return 0;
	}

	const void *args;
	void *obj;

	if (ffstr_eqz(&modname, "core")) {
		args = conf_args;
		obj = conf;

	} else {
		if (ffstr_splitby(&key, '.', &modname, &key) < 0) {
			infolog0("user config: bad key %S", keyname);
			return 0;
		}
		uint n = modname.ptr+modname.len - keyname->ptr;
		ffstr_set(&modname, keyname->ptr, n);
		if (!allowed_mod(&modname))
			return 0;

		core_mod *mod;
		if (NULL == (mod = (void*)core_getmodinfo(modname))) {
			infolog0("user config: unknown module: %S", &modname);
			return 0;
		}

		if (!mod->have_conf) {
			infolog0("user config: module doesn't support configuration: %S", &modname);
			return 0;
		}

		if (mod->conf_ctx.args == NULL) {
			// the module isn't yet loaded - just store all its user settings
			ffvec_addfmt(&mod->usrconf_data, "%S %S\n", &key, val);
			return 0;
		}

		args = mod->conf_ctx.args;
		obj = mod->conf_ctx.obj;
	}

	ffconf c2;
	ffconf_init(&c2);
	ffconf_scheme sc;
	ffconf_scheme_init(&sc, &c2);
	ffconf_scheme_addctx(&sc, args, obj);
	usrconf_read(&sc, key, *val);
	ffconf_fin(&c2);
	ffconf_scheme_destroy(&sc);
	return 0;
}

/** Process one key-value pair from user config */
void usrconf_read(ffconf_scheme *sc, ffstr key, ffstr val)
{
	int r;
	ffstr ctx;

	if (ffstr_splitby(&key, '.', &ctx, &key) >= 0) {
		sc->parser->val = ctx;
		r = ffconf_scheme_process(sc, FFCONF_RKEY);
		if (r < 0)
			goto end;

		r = ffconf_scheme_process(sc, FFCONF_ROBJ_OPEN);
		if (r < 0)
			goto end;
	} else {
		key = ctx;
	}

	sc->parser->val = key;
	r = ffconf_scheme_process(sc, FFCONF_RKEY);
	if (r < 0)
		goto end;

	sc->parser->val = val;
	r = ffconf_scheme_process(sc, FFCONF_RVAL);

end:
	if (r < 0)
		infolog0("user config: key %S: %s", &key, sc->errmsg);
}

static const fmed_conf_arg confusr_args[] = {
	{ "*",	FFCONF_TSTR | FFCONF_FLIST | FFCONF_FMULTI, FMC_F(confusr_mod) },
	{}
};


static int ext_map_keyeq_func(void *opaque, const void *key, ffsize keylen, void *val)
{
	const inmap_item *it = val;
	return !ffs_icmpz(key, keylen, it->ext);
}

const fmed_modinfo* modbyext(const ffmap *map, const ffstr *ext)
{
	const inmap_item *it = ffmap_find(map, ext->ptr, ext->len, NULL);
	if (it == NULL)
		return NULL;
	return it->mod;
}

static void inout_ext_map_init(ffmap *map, ffslice *arr)
{
	ffuint n = 0;
	const inmap_item *it;
	for (it = arr->ptr;  it != (void*)(arr->ptr + arr->len)
		;  it = (inmap_item*)((char*)it + sizeof(inmap_item) + ffsz_len(it->ext)+1)) {
		n++;
	}

	ffmap_init(map, ext_map_keyeq_func);
	ffmap_alloc(map, n);
	for (it = arr->ptr;  it != (void*)(arr->ptr + arr->len)
		;  it = (inmap_item*)((char*)it + sizeof(inmap_item) + ffsz_len(it->ext)+1)) {
		ffstr ext = FFSTR_INITZ(it->ext);
		ffmap_add(map, ext.ptr, ext.len, (void*)it);
	}
}


int core_conf_parse(fmed_config *conf, const char *filename, uint flags)
{
	ffltconf pconf;
	fmed_conf ps = {};
	int r = FMC_ESYS;
	ffstr s;
	ffvec buf = {};

	ffltconf_init(&pconf);
	ffconf_scheme_init(&ps, &pconf.ff);
	if (flags & CONF_F_USR)
		ffconf_scheme_addctx(&ps, confusr_args, conf);
	else
		ffconf_scheme_addctx(&ps, conf_args, conf);

	if (0 != fffile_readwhole(filename, &buf, 1*1024*1024)) {
		if (fferr_nofile(fferr_last()) && (flags & CONF_F_OPT)) {
			r = 0;
			goto fail;
		}
		syserrlog("%s: %s", fffile_open_S, filename);
		goto fail;
	}

	dbglog(core, NULL, "core", "reading config file %s", filename);

	{
		ffstr_setstr(&s, &buf);

		while (s.len != 0) {
			ffstr val;
			r = ffltconf_parse3(&pconf, &s, &val);
			if (r < 0)
				goto err;

			if (conf->conf_copy_mod != NULL) {
				int r2 = ffconf_ctx_copy(&conf->conf_copy, val, r);
				if (r2 < 0) {
					errlog0("parse config: %s: %u:%u: ffconf_ctx_copy()"
						, filename
						, pconf.ff.line, pconf.ff.linechar);
					goto fail;
				} else if (r2 > 0) {
					core_mod *m = (void*)conf->conf_copy_mod;
					m->conf_data = ffconf_ctxcopy_acquire(&conf->conf_copy);
					conf->conf_copy_mod = NULL;
				}
				continue;
			}

			r = ffconf_scheme_process(&ps, r);

			if (r < 0 && r != -CONF_DELAYED)
				goto err;
		}
	}

	r = ffltconf_fin(&pconf);

err:
	if (r < 0) {
		const char *ser = ffltconf_error(&pconf);
		if (r == -FFCONF_ESCHEME)
			ser = ps.errmsg;
		errlog(core, NULL, "core"
			, "parse config: %s: %u:%u: near \"%S\": \"%s\": %s"
			, filename
			, pconf.ff.line, pconf.ff.linechar
			, &pconf.ff.val, (ps.arg != NULL) ? ps.arg->name : ""
			, (r == FMC_ESYS) ? fferr_strp(fferr_last()) : ser);
		goto fail;
	}

	r = 0;

	if (!(flags & CONF_F_USR)) {
		inout_ext_map_init(&conf->in_ext_map, (ffslice*)&conf->inmap);
		inout_ext_map_init(&conf->out_ext_map, (ffslice*)&conf->outmap);
	}

fail:
	conf->conf_copy_mod = NULL;
	ffconf_ctxcopy_destroy(&conf->conf_copy);
	ffltconf_fin(&pconf);
	ffconf_scheme_destroy(&ps);
	ffvec_free(&buf);
	return r;
}
