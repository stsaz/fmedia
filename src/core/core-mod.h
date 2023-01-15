/** fmedia: core: modules
2015,2021, Simon Zolin */

#include <util/conf2-ltconf.h>

#define FMED_VER_GETMAJ(fullver)  ((fullver & 0xff0000) >> 16)
#define FMED_VER_GETMIN(fullver)  ((fullver & 0xff00) >> 8)
#define FMED_VER_GETPATCH(fullver)  (fullver & 0xff)

void usrconf_read(ffconf_scheme *sc, ffstr key, ffstr val);

static void core_posted(void *udata)
{
}

static void somod_destroy(core_modinfo *m)
{
	ffmem_safefree(m->name);
	if (m->m != NULL)
		m->m->destroy();
	if (m->dl != NULL)
		ffdl_close(m->dl);
}

/** Get pointer for a new module. */
static core_modinfo* somod_create(const ffstr *soname)
{
	core_modinfo *bmod;
	if (NULL == (bmod = ffvec_pushT(&fmed->bmods, core_modinfo)))
		goto fail;
	ffmem_zero_obj(bmod);
	if (NULL == (bmod->name = ffsz_alcopystr(soname)))
		goto fail;
	return bmod;

fail:
	somod_destroy(bmod);
	return NULL;
}

/** Load module (internal or external).
. load .so module
. get its fmedia module interface
. check if module's version is supported
. send 'initialize' signal */
static int somod_load(core_modinfo *minfo)
{
	int rc = -1;
	fmed_getmod_t getmod;
	const fmed_mod *m;
	ffdl dl = NULL;
	ffstr s;
	char *fn = NULL;
	ffbool locked = 0;

	ffstr_setz(&s, minfo->name);

	if (s.ptr[0] == '#') {
		if (ffstr_eqcz(&s, "#core"))
			getmod = &fmed_getmod_core;
		else if (ffstr_eqcz(&s, "#file"))
			getmod = &fmed_getmod_file;
		else if (ffstr_eqcz(&s, "#queue"))
			getmod = &fmed_getmod_queue;
#ifdef FF_WIN
		else if (ffstr_eqcz(&s, "#winsleep"))
			getmod = &fmed_getmod_winsleep;
#endif
		else if (ffstr_eqcz(&s, "#globcmd")) {
			getmod = &fmed_getmod_globcmd;
		} else {
			fferr_set(EINVAL);
			goto fail;
		}

	} else {
		if (NULL == (fn = ffsz_allocfmt("%Smod%c%S.%s"
			, &fmed->root, FFPATH_SLASH, &s, FFDL_EXT)))
			goto fail;

		dl = ffdl_open(fn, FFDL_SELFDIR);
		if (dl == NULL) {
			errlog0("module %s: %s: %s", fn, ffdl_open_S, ffdl_errstr());
			goto fail;
		}

		getmod = (void*)ffdl_addr(dl, FMED_MODFUNCNAME);
		if (getmod == NULL) {
			errlog0("module %s: %s '%s': %s"
				, fn, ffdl_addr_S, FMED_MODFUNCNAME, ffdl_errstr());
			goto fail;
		}
	}

	fflk_lock(&minfo->lock);
	locked = 1;
	if (minfo->m != NULL) {
		// another thread has just opened this module
		ffdl_close(dl);
		dl = NULL;
		rc = 0;
		goto fail;
	}

	m = getmod(core);
	if (m == NULL)
		goto fail;

	if (m->ver_core != FMED_VER_CORE) {
		errlog0("can't load module %s v%u.%u.%u because it's built for core v%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver), FMED_VER_GETPATCH(m->ver)
			, FMED_VER_GETMAJ(m->ver_core), FMED_VER_GETMIN(m->ver_core));
		goto fail;
	}

	if (s.ptr[0] != '#') {
		dbglog0("loaded module %s v%u.%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver), FMED_VER_GETPATCH(m->ver));
	}

	if (0 != m->sig(FMED_SIG_INIT))
		goto fail;

	minfo->m = m;
	minfo->dl = dl;
	rc = 0;

fail:
	if (locked)
		fflk_unlock(&minfo->lock);
	if (rc != 0) {
		FF_SAFECLOSE(dl, NULL, ffdl_close);
	}
	ffmem_free(fn);
	return rc;
}

/** Get pointer for a new module interface. */
static core_mod* mod_createiface(ffstr name)
{
	core_mod *mod;
	if (NULL == (mod = ffmem_calloc(1, sizeof(core_mod) + name.len + 1)))
		return NULL;
	ffsz_fcopy(mod->name_s, name.ptr, name.len);
	mod->name = mod->name_s;
	return mod;
}

static void mod_freeiface(core_mod *m)
{
	ffstr_free(&m->conf_data);
	ffvec_free(&m->usrconf_data);
	ffmem_free(m);
}

/** Load module interface
Note: not thread-safe */
static int mod_loadiface(core_mod *mod, const core_modinfo *bmod)
{
	ffstr soname, modname;
	ffs_split2by(mod->name, ffsz_len(mod->name), '.', &soname, &modname);

	if (NULL == (mod->iface = bmod->m->iface(modname.ptr))) {
		errlog0("can't initialize %s", mod->name);
		goto end;
	}

	mod->dl = bmod->dl;
	mod->m = bmod->m;
	return 0;

end:
	return -1;
}

static core_modinfo* core_findmod(const ffstr *name)
{
	core_modinfo *mod;
	FFSLICE_WALK(&fmed->bmods, mod) {
		if (ffstr_eqz(name, mod->name))
			return mod;
	}
	return NULL;
}

const fmed_modinfo* core_insmod(ffstr name, ffconf_scheme *fc)
{
	core_mod *mod = (void*)core_getmodinfo(name);
	if (mod != NULL) {
		return NULL;
	}

	ffstr so, modname;
	ffstr_splitby(&name, '.', &so, &modname);
	if (so.len == 0 || modname.len == 0) {
		fferr_set(EINVAL);
		goto fail;
	}

	core_modinfo *minfo;
	if (NULL == (minfo = core_findmod(&so))) {
		if (NULL == (minfo = somod_create(&so)))
			goto fail;
	}

	if (minfo->m == NULL && 0 != somod_load(minfo)) {
		somod_destroy(minfo);
		fmed->bmods.len--;
		goto fail;
	}

	if (NULL == (mod = mod_createiface(name)))
		goto fail;

	if (0 != mod_loadiface(mod, minfo))
		goto fail;

	if (fc != NULL) {
		if (minfo->m->conf == NULL)
			goto fail;
		fmed_conf_ctx ctx = {};
		const char *modnamez = mod->name + so.len + 1;
		if (0 != minfo->m->conf(modnamez, &ctx))
			goto fail;
		mod->conf_ctx = ctx;
		ffconf_scheme_addctx(fc, ctx.args, ctx.obj);
	}

	fflist_ins(&fmed->mods, &mod->sib);
	return (fmed_modinfo*)mod;

fail:
	if (mod != NULL) {
		mod_freeiface(mod);
	}
	return NULL;
}

const fmed_modinfo* core_insmodz(const char *name, ffconf_scheme *fc)
{
	ffstr nm = FFSTR_INITZ(name);
	return core_insmod(nm, fc);
}

/** Enlist a new module which will be loaded later. */
const fmed_modinfo* core_insmod_delayed(ffstr name)
{
	ffstr soname, modname;
	ffstr_splitby(&name, '.', &soname, &modname);
	if (soname.len == 0 || modname.len == 0) {
		fferr_set(EINVAL);
		return NULL;
	}

	core_modinfo *m;
	if (NULL == (m = core_findmod(&soname))) {
		if (NULL == (m = somod_create(&soname)))
			return NULL;
	}

	core_mod *mod = mod_createiface(name);
	fflist_ins(&fmed->mods, &mod->sib);
	return (void*)mod;
}

const fmed_modinfo* core_getmodinfo(ffstr name)
{
	core_mod *mod;
	_FFLIST_WALK(&fmed->mods, mod, sib) {
		if (ffstr_eqz(&name, mod->name))
			return (fmed_modinfo*)mod;
	}
	return NULL;
}

/** Process user config settings stored previously for this module */
static int mod_usrconf_read(core_mod *mod)
{
	ffconf c = {};
	ffconf_init(&c);
	ffconf_scheme sc = {};
	ffconf_scheme_init(&sc, &c);
	ffconf_scheme_addctx(&sc, mod->conf_ctx.args, mod->conf_ctx.obj);

	ffstr d = FFSTR_INITSTR(&mod->usrconf_data);
	while (d.len != 0) {
		ffstr ln, key, val;
		ffstr_splitby(&d, '\n', &ln, &d);
		ffstr_splitby(&ln, ' ', &key, &val);
		if (key.len == 0 || val.len == 0)
			continue;
		usrconf_read(&sc, key, val);
	}

	ffconf_fin(&c);
	ffconf_scheme_destroy(&sc);
	ffvec_free(&mod->usrconf_data);
	return 0;
}

/** Read module's configuration parameters. */
static int mod_readconf(core_mod *mod, const char *name)
{
	int r, rc = -1;
	ffltconf conf = {};
	ffconf_scheme ps = {};

	if (mod->m->conf == NULL)
		return -1;

	ffltconf_init(&conf);
	ffconf_scheme_init(&ps, &conf.ff);

	fmed_conf_ctx ctx = {};
	if (0 != mod->m->conf(name, &ctx))
		return -1;
	mod->conf_ctx = ctx;
	ffconf_scheme_addctx(&ps, ctx.args, ctx.obj);

	ffstr data = mod->conf_data;
	while (data.len != 0) {
		r = ffltconf_parse(&conf, &data);
		r = ffconf_scheme_process(&ps, r);
		if (r < 0)
			goto done;
	}

	r = ffltconf_fin(&conf);
	if (r < 0)
		goto done;

	ffstr_free(&mod->conf_data);

	rc = mod_usrconf_read(mod);

done:
	if (rc != 0) {
		errlog0("can't load module %s: config parser: %u:%u: \"%s\": %s"
			, mod->name, conf.ff.line, conf.ff.linechar
			, (ps.arg != NULL) ? ps.arg->name : ""
			, ffconf_errstr(r));
	}
	ffltconf_fin(&conf);
	ffconf_scheme_destroy(&ps);
	return rc;
}

/** Actually load a module. */
static int mod_load_delayed(core_mod *mod)
{
	ffbool locked = 0;
	ffstr soname, modname;
	ffs_split2by(mod->name, ffsz_len(mod->name), '.', &soname, &modname);

	core_modinfo *bmod;
	if (NULL == (bmod = core_findmod(&soname)))
		goto fail;
	if (bmod->m == NULL && 0 != somod_load(bmod))
		goto fail;

	if (0 != mod_loadiface(mod, bmod))
		goto end;

	fflk_lock(&mod->lock);
	locked = 1;
	if (mod->have_conf) {
		if (0 != mod_readconf(mod, modname.ptr))
			goto end;
	}

	if (!bmod->opened) {
		dbglog0("signal:%u for module %s", (int)FMED_OPEN, bmod->name);
		if (0 != bmod->m->sig(FMED_OPEN))
			goto fail;
		bmod->opened = 1;
	}

	fflk_unlock(&mod->lock);
	return 0;

fail:
	errlog0("can't load module %s", mod->name);
end:
	if (locked)
		fflk_unlock(&mod->lock);
	mod->dl = NULL;
	mod->m = NULL;
	mod->iface = NULL;
	return -1;
}

static const void* core_getmod2(uint flags, const char *name, ssize_t name_len)
{
	const fmed_modinfo *mod;
	ffstr s;
	uint t = flags & 0xff;

	if (name_len == -1)
		ffstr_setz(&s, name);
	else
		ffstr_set(&s, name, name_len);

	switch (t) {

	case FMED_MOD_SOINFO: {
		core_modinfo *m = core_findmod(&s);
		if (0 != somod_load(m))
			return NULL;
		return m;
	}

	case FMED_MOD_INEXT:
		mod = modbyext(&fmed->conf.in_ext_map, &s);
		if (mod == NULL) {
			dbglog0("unknown input file format: %S.  Will detect format from data.", &s);
			ffstr nm = FFSTR_INITZ("#core.format-detector");
			mod = core_getmodinfo(nm);
		}
		flags |= FMED_MOD_NOLOG;
		break;

	case FMED_MOD_OUTEXT:
		mod = modbyext(&fmed->conf.out_ext_map, &s);
		if (mod == NULL) {
			if (s.len == 0)
				errlog0("Please specify output file extension");
			else
				errlog0("output file extension not supported: '%S'", &s);
		}
		flags |= FMED_MOD_NOLOG;
		break;

	case FMED_MOD_IFACE:
		mod = core_getmodinfo(s);
		break;

	case FMED_MOD_INFO_ADEV_IN:
		mod = fmed->conf.input;
		break;

	case FMED_MOD_INFO_ADEV_OUT:
		mod = fmed->conf.output;
		break;

	default:
		goto err;
	}

	if (mod == NULL)
		goto err;
	if (mod->m == NULL && 0 != mod_load_delayed((core_mod*)mod))
		return NULL;
	if (t == FMED_MOD_IFACE)
		return mod->iface;
	return mod;

err:
	if (!(flags & FMED_MOD_NOLOG))
		errlog0("module not found: %S", &s);
	return NULL;
}

static const void* core_getmod(const char *name)
{
	return core_getmod2(FMED_MOD_IFACE | FMED_MOD_NOLOG, name, -1);
}

static int core_sigmods(uint signo)
{
	core_modinfo *mod;
	FFSLICE_WALK(&fmed->bmods, mod) {
		if (mod->m == NULL)
			continue;
		if (signo == FMED_OPEN && mod->opened)
			continue;
		dbglog0("signal:%u for module %s", signo, mod->name);
		if (0 != mod->m->sig(signo))
			return 1;
		if (signo == FMED_OPEN)
			mod->opened = 1;
	}
	return 0;
}
