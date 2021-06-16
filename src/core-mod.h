/** fmedia: core: modules
2015,2021, Simon Zolin */

static void core_posted(void *udata)
{
}

static void mod_destroy(core_modinfo *m)
{
	ffmem_safefree(m->name);
	if (m->m != NULL)
		m->m->destroy();
	if (m->dl != NULL)
		ffdl_close(m->dl);
}

/** Get pointer for a new module. */
static core_modinfo* mod_create(const ffstr *soname)
{
	core_modinfo *bmod;
	if (NULL == (bmod = ffvec_pushT(&fmed->bmods, core_modinfo)))
		goto fail;
	ffmem_zero_obj(bmod);
	if (NULL == (bmod->name = ffsz_alcopystr(soname)))
		goto fail;
	return bmod;

fail:
	mod_destroy(bmod);
	return NULL;
}

/** Load module (internal or external).
. load .so module
. get its fmedia module interface
. check if module's version is supported
. send 'initialize' signal */
static int mod_load(core_modinfo *minfo)
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
		else if (ffstr_eqcz(&s, "#soundmod"))
			getmod = &fmed_getmod_sndmod;
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
		errlog0("module %s v%u.%u: module is built for core v%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver)
			, FMED_VER_GETMAJ(m->ver_core), FMED_VER_GETMIN(m->ver_core));
		goto fail;
	}

	if (s.ptr[0] != '#') {
		dbglog0("loaded module %s v%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver));
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
static core_mod* mod_createiface(const ffstr *name)
{
	core_mod *mod;
	if (NULL == (mod = ffmem_calloc(1, sizeof(core_mod) + name->len + 1)))
		return NULL;
	ffsz_fcopy(mod->name_s, name->ptr, name->len);
	mod->name = mod->name_s;
	return mod;
}

static void mod_freeiface(core_mod *m)
{
	ffstr_free(&m->conf_data);
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

static const fmed_modinfo* core_insmod(const char *sname, ffpars_ctx *ctx)
{
	core_mod *mod = NULL;
	ffstr s, modname;
	ffstr name;
	ffstr_setz(&name, sname);

	ffs_split2by(name.ptr, name.len, '.', &s, &modname);
	if (s.len == 0 || modname.len == 0) {
		fferr_set(EINVAL);
		goto fail;
	}

	core_modinfo *minfo;
	if (NULL == (minfo = core_findmod(&s))) {
		if (NULL == (minfo = mod_create(&s)))
			goto fail;
	}

	if (minfo->m == NULL && 0 != mod_load(minfo)) {
		mod_destroy(minfo);
		fmed->bmods.len--;
		goto fail;
	}

	if (NULL == (mod = mod_createiface(&name)))
		goto fail;
	if (0 != mod_loadiface(mod, minfo))
		goto fail;

	if (ctx != NULL) {
		if (minfo->m->conf == NULL)
			goto fail;
		if (0 != minfo->m->conf(modname.ptr, ctx))
			goto fail;
		mod->conf_ctx = *ctx;
	}

	fflist_ins(&fmed->mods, &mod->sib);
	return (fmed_modinfo*)mod;

fail:
	if (mod != NULL) {
		mod_freeiface(mod);
	}
	return NULL;
}

/** Enlist a new module which will be loaded later. */
const fmed_modinfo* core_insmod_delayed(const char *sname, ffpars_ctx *ctx)
{
	core_mod *mod;
	core_modinfo *bmod = NULL;
	ffstr name;
	ffstr_setz(&name, sname);

	if (sname[0] == '#')
		return core_insmod(sname, ctx);

	ffstr soname, modname;
	ffs_split2by(name.ptr, name.len, '.', &soname, &modname);
	if (soname.len == 0 || modname.len == 0) {
		fferr_set(EINVAL);
		goto fail;
	}

	if (NULL == (bmod = core_findmod(&soname))) {
		if (NULL == (bmod = mod_create(&soname)))
			goto fail;
	}

	if (NULL == (mod = mod_createiface(&name)))
		goto fail;
	fflist_ins(&fmed->mods, &mod->sib);

	return (void*)mod;

fail:
	if (bmod != NULL) {
		mod_destroy(bmod);
		fmed->bmods.len--;
	}
	return NULL;
}

const fmed_modinfo* core_getmodinfo(const ffstr *name)
{
	core_mod *mod;
	_FFLIST_WALK(&fmed->mods, mod, sib) {
		if (ffstr_eqz(name, mod->name))
			return (fmed_modinfo*)mod;
	}
	return NULL;
}

static const fmed_modinfo* core_modbyext(const ffslice *map, const ffstr *ext)
{
	const inmap_item *it = (void*)map->ptr;
	while (it != (void*)(map->ptr + map->len)) {
		size_t len = ffsz_len(it->ext);
		if (ffstr_ieq(ext, it->ext, len)) {
			return it->mod;
		}
		it = (void*)((char*)it + sizeof(inmap_item) + len + 1);
	}

	return NULL;
}

/** Read module's configuration parameters. */
static int mod_readconf(core_mod *mod, const char *name)
{
	int r;
	ffparser_schem ps;
	ffconf conf;

	if (mod->m->conf == NULL)
		return -1;

	ffpars_ctx ctx;
	if (0 != mod->m->conf(name, &ctx))
		return -1;

	if (0 != (r = ffconf_scheminit(&ps, &conf, &ctx)))
		goto fail;
	mod->conf_ctx = ctx;

	ffstr data = mod->conf_data;
	while (data.len != 0) {
		r = ffconf_parsestr(&conf, &data);
		r = ffconf_schemrun(&ps);
		if (ffpars_iserr(r))
			goto fail;
	}

	r = ffconf_schemfin(&ps);
	if (ffpars_iserr(r))
		goto fail;

	ffstr_free(&mod->conf_data);
	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);
	return 0;

fail:
	errlog0("can't load module %s: config parser: %u:%u: \"%s\": %s"
		, mod->name, conf.line, conf.ch
		, (ps.curarg != NULL) ? ps.curarg->name : ""
		, ffpars_errstr(r));
	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);
	return -1;
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
	if (bmod->m == NULL && 0 != mod_load(bmod))
		goto fail;

	if (0 != mod_loadiface(mod, bmod))
		goto end;

	fflk_lock(&mod->lock);
	locked = 1;
	if (mod->have_conf) {
		if (0 != mod_readconf(mod, modname.ptr))
			goto end;
		mod->have_conf = 0;
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
	ffstr s, smod, iface;
	uint t = flags & 0xff;

	if (name_len == -1)
		ffstr_setz(&s, name);
	else
		ffstr_set(&s, name, name_len);

	switch (t) {

	case FMED_MOD_INEXT:
		mod = core_modbyext((ffslice*)&fmed->conf.inmap, &s);
		if (mod == NULL) {
			dbglog0("unknown input file format: %S.  Will detect format from data.", &s);
			ffstr nm = FFSTR_INITZ("#core.format-detector");
			mod = core_getmodinfo(&nm);
		}
		flags |= FMED_MOD_NOLOG;
		break;

	case FMED_MOD_OUTEXT:
		mod = core_modbyext((ffslice*)&fmed->conf.outmap, &s);
		if (mod == NULL)
			errlog0("unknown output file format: %S", &s);
		flags |= FMED_MOD_NOLOG;
		break;

	case FMED_MOD_INFO:
		mod = core_getmodinfo(&s);
		break;

	case FMED_MOD_IFACE:
		if (NULL == (mod = core_getmodinfo(&s)))
			goto err;
		if (mod->m == NULL && 0 != mod_load_delayed((core_mod*)mod))
			return NULL;
		return mod->iface;

	case FMED_MOD_SOINFO:
	case FMED_MOD_IFACE_ANY: {
		core_modinfo *mi;
		const void *fc;
		ffs_split2by(s.ptr, s.len, '.', &smod, &iface);
		if (NULL == (mi = (void*)core_findmod(&smod)))
			goto err;
		if (mi->m == NULL) {
			if (0 != mod_load(mi))
				goto err;
		}
		if (t == FMED_MOD_SOINFO)
			return mi;
		if (iface.len == 0)
			goto err;
		if (NULL == (fc = mi->m->iface(iface.ptr)))
			goto err;
		return fc;
	}

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
	return mod;

err:
	if (!(flags & FMED_MOD_NOLOG))
		errlog0("module not found: %S", &s);
	return NULL;
}

static const void* core_getmod(const char *name)
{
	const void *m = core_getmod2(FMED_MOD_IFACE | FMED_MOD_NOLOG, name, -1);
	if (m != NULL)
		return m;
	return core_getmod2(FMED_MOD_IFACE_ANY, name, -1);
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
