/** fmedia core.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/crc.h>
#include <FF/path.h>
#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/time.h>
#include <FF/sys/filemap.h>
#include <FFOS/error.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>
#include <FFOS/dir.h>


#define syswarnlog(trk, ...)  fmed_syswarnlog(core, NULL, "core", __VA_ARGS__)


typedef struct inmap_item {
	const fmed_modinfo *mod;
	char ext[0];
} inmap_item;

typedef struct core_modinfo {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
} core_modinfo;

typedef struct core_mod {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const void *iface;

	ffpars_ctx conf_ctx;
	fflist_item sib;
	char name_s[0];
} core_mod;


fmedia *fmed;

enum {
	FMED_KQ_EVS = 8,
	CONF_MBUF = 2 * 4096,
	TMR_INT = 250,
};


FF_EXP fmed_core* core_init(fmed_cmd **ptr, char **argv, char **env);
FF_EXP void core_free(void);

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_globcmd(const fmed_core *_core);

static int core_open(void);
static int core_sigmods(uint signo);
static core_modinfo* core_findmod(const ffstr *name);
static const fmed_modinfo* core_getmodinfo(const ffstr *name);
static const fmed_modinfo* core_modbyext(const ffstr3 *map, const ffstr *ext);
static core_modinfo* mod_load(const ffstr *ps);
static void mod_destroy(core_modinfo *m);
static int core_filetype(const char *fn);

static const void* core_iface(const char *name);
static int core_sig2(uint signo);
static void core_destroy(void);
static const fmed_mod fmed_core_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&core_iface, &core_sig2, &core_destroy
};

// CORE
static int64 core_getval(const char *name);
static void core_log(uint flags, void *trk, const char *module, const char *fmt, ...);
static char* core_getpath(const char *name, size_t len);
static char* core_env_expand(char *dst, size_t cap, const char *src);
static int core_sig(uint signo);
static ssize_t core_cmd(uint cmd, ...);
static const void* core_getmod(const char *name);
const void* core_getmod2(uint flags, const char *name, ssize_t name_len);
static const fmed_modinfo* core_insmod(const char *name, ffpars_ctx *ctx);
static void core_task(fftask *task, uint cmd);
static int core_timer(fftmrq_entry *tmr, int64 interval, uint flags);

static fmed_core _fmed_core = {
	0, NULL, 0,
	&core_getval,
	&core_log,
	&core_getpath, &core_env_expand,
	&core_sig, &core_cmd,
	&core_getmod, &core_getmod2, &core_insmod,
	&core_task,
	.timer = &core_timer,
};
fmed_core *core = &_fmed_core;

//LOG
static void log_dummy_func(uint flags, fmed_logdata *ld)
{
}
static const fmed_log log_dummy = {
	&log_dummy_func
};

enum {
	CONF_F_USR = 1,
	CONF_F_OPT = 2,
};

static int fmed_conf(const char *fn);
static int fmed_conf_fn(const char *filename, uint flags);
static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_input(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_recfmt(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_inp_channels(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_ext_val(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_codepage(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_include(ffparser_schem *p, void *obj, ffstr *val);

// enum FMED_INSTANCE_MODE
static const char *const im_enumstr[] = {
	"off", "add", "play", "clear_play"
};
static const ffpars_enumlist im_enum = { im_enumstr, FFCNT(im_enumstr), FFPARS_DSTOFF(fmed_config, instance_mode) };

static const ffpars_arg fmed_conf_args[] = {
	{ "mod",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_mod) }
	, { "mod_conf",  FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_modconf) }
	, { "output",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_output) }
	, { "input",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_input) }
	, { "record_format",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_recfmt) }
	, { "input_ext",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_ext) }
	, { "output_ext",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_ext) }
	, { "codepage",  FFPARS_TSTR, FFPARS_DST(&fmed_conf_codepage) }
	, { "instance_mode",  FFPARS_TENUM | FFPARS_F8BIT, FFPARS_DST(&im_enum) }
	,
	{ "include",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_include) },
	{ "include_user",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_include) },
};

static int fmed_confusr_mod(ffparser_schem *ps, void *obj, ffstr *val);

static const ffpars_arg fmed_confusr_args[] = {
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI, FFPARS_DST(&fmed_confusr_mod) },
};


static int allowed_mod(const ffstr *name)
{
#if defined FF_WIN
	if (ffstr_matchcz(name, "alsa.")
		|| ffstr_matchcz(name, "pulse.")
		|| ffstr_matchcz(name, "oss."))
#elif defined FF_LINUX
	if (ffstr_matchcz(name, "wasapi.")
		|| ffstr_matchcz(name, "direct-sound.")
		|| ffstr_matchcz(name, "oss."))
#elif defined FF_BSD
	if (ffstr_matchcz(name, "wasapi.")
		|| ffstr_matchcz(name, "direct-sound.")
		|| ffstr_matchcz(name, "alsa.")
		|| ffstr_matchcz(name, "pulse."))
#endif
		return 0;
	return 1;
}

static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	if (ffstr_matchcz(val, "gui.") && !fmed->cmd.gui)
		goto done;

	if (NULL == core->insmod(val->ptr, NULL))
		return FFPARS_ESYS;

done:
	ffstr_free(val);
	return 0;
}

static int fmed_conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	const ffstr *name = &p->vals[0];
	char *zname;

	if (!allowed_mod(name)) {
		ffpars_ctx_skip(ctx);
		return 0;
	}

	if (ffstr_eqcz(name, "tui.tui") && (fmed->cmd.notui || fmed->cmd.gui)) {
		ffpars_ctx_skip(ctx);
		return 0;
	}

	if (ffstr_eqcz(name, "gui.gui") && !fmed->cmd.gui) {
		ffpars_ctx_skip(ctx);
		return 0;
	}

	zname = ffsz_alcopy(name->ptr, name->len);
	if (zname == NULL)
		return FFPARS_ESYS;

	if (NULL == core->insmod(zname, ctx)) {
		ffmem_free(zname);
		return FFPARS_ESYS;
	}
	ffmem_free(zname);
	return 0;
}

static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val))
		return 0;

	if (NULL == (conf->output = core_getmod2(FMED_MOD_INFO, val->ptr, val->len)))
		return FFPARS_EBADVAL;
	return 0;
}

static int fmed_conf_input(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val))
		return 0;

	if (NULL == (conf->input = core_getmod2(FMED_MOD_INFO, val->ptr, val->len)))
		return FFPARS_EBADVAL;
	return 0;
}

static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int r;
	if (0 > (r = ffpcm_fmt(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->inp_pcm.format = r;
	return 0;
}

static int fmed_conf_inp_channels(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int r;
	if (0 > (r = ffpcm_channels(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	conf->inp_pcm.channels = r;
	return 0;
}

static const ffpars_arg fmed_conf_input_args[] = {
	{ "format",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_inp_format) }
	, { "channels",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_inp_channels) }
	, { "rate",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(fmed_config, inp_pcm.sample_rate) }
};

static int fmed_conf_recfmt(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	ffpars_setargs(ctx, conf, fmed_conf_input_args, FFCNT(fmed_conf_input_args));
	return 0;
}

static int fmed_conf_ext_val(ffparser_schem *p, void *obj, ffstr *val)
{
	size_t n;
	inmap_item *it;
	ffstr3 *map = obj;

	if (p->p->type == FFCONF_TKEY) {
		const fmed_modinfo *mod = core_getmod2(FMED_MOD_INFO, val->ptr, val->len);
		if (mod == NULL) {
			return FFPARS_EBADVAL;
		}
		fmed->conf.inmap_curmod = mod;
		return 0;
	}

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffarr_grow(map, n, 64 | FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	it = (void*)(map->ptr + map->len);
	map->len += n;
	it->mod = fmed->conf.inmap_curmod;
	ffsz_fcopy(it->ext, val->ptr, val->len);
	return 0;
}

static const ffpars_arg fmed_conf_ext_args[] = {
	{ "*",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FLIST, FFPARS_DST(&fmed_conf_ext_val) }
};

static int fmed_conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	void *o = &conf->inmap;
	if (!ffsz_cmp(p->curarg->name, "output_ext"))
		o = &conf->outmap;
	ffpars_setargs(ctx, o, fmed_conf_ext_args, FFCNT(fmed_conf_ext_args));
	return 0;
}

static int fmed_conf_codepage(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	int cp = ffu_coding(val->ptr, val->len);
	if (cp == -1)
		return FFPARS_EBADVAL;
	conf->codepage = cp;
	return 0;
}

static int fmed_conf_include(ffparser_schem *p, void *obj, ffstr *val)
{
	int r = FFPARS_EBADVAL;
	char *fn = NULL;
	ffarr name = {0};

	if (!ffsz_cmp(p->curarg->name, "include")) {
		if (NULL == (fn = core->getpath(val->ptr, val->len))) {
			r = FFPARS_ESYS;
			goto end;
		}

	} else {
		if (0 == ffstr_catfmt(&name, "%s/fmedia/%S%Z", FFDIR_USER_CONFIG, val)) {
			r = FFPARS_ESYS;
			goto end;
		}
		if (NULL == (fn = ffenv_expand(&fmed->env, NULL, 0, name.ptr))) {
			r = FFPARS_ESYS;
			goto end;
		}
	}

	if (0 != fmed_conf_fn(fn, CONF_F_USR | CONF_F_OPT))
		goto end;

	r = 0;
end:
	ffmem_safefree(fn);
	ffarr_free(&name);
	return r;
}

/** Process "so.modname" part from "so.modname.key value" */
static int fmed_confusr_mod(ffparser_schem *ps, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	const core_mod *mod;
	int r;

	if (conf->skip_line) {
		if (ps->p->type == FFCONF_TVAL) {
			conf->skip_line = 0;
		}
		return 0;
	}

	if (ps->p->type != FFCONF_TKEYCTX)
		return FFPARS_EUKNKEY;

	if (ffstr_eqcz(val, "core"))
		ffpars_setctx(ps, conf, fmed_conf_args, FFCNT(fmed_conf_args));

	else if (conf->usrconf_modname == NULL) {
		if (ffstr_eqcz(val, "gui") && !fmed->cmd.gui) {
			conf->skip_line = 1;
			return 0;
		}
		if (NULL == (conf->usrconf_modname = ffsz_alcopy(val->ptr, val->len)))
			return FFPARS_ESYS;

	} else {
		r = 0;
		ffstr3 s = {0};
		if (0 == ffstr_catfmt(&s, "%s.%S", conf->usrconf_modname, val))
			return FFPARS_ESYS;
		if (NULL == (mod = core_getmod2(FMED_MOD_INFO, s.ptr, s.len))) {
			r = FFPARS_EBADVAL;
			goto end;
		}

		if (mod->conf_ctx.args == NULL) {
			r = FFPARS_EBADVAL;
			goto end;
		}
		ffpars_setctx(ps, mod->conf_ctx.obj, mod->conf_ctx.args, mod->conf_ctx.nargs);

end:
		ffmem_free0(conf->usrconf_modname);
		ffarr_free(&s);
		if (r != 0)
			return r;
	}

	return 0;
}

static int fmed_conf(const char *filename)
{
	int r = -1;
	char *fn;

	if (filename != NULL)
		fn = (void*)filename;
	else if (NULL == (fn = core->getpath(FFSTR(FMED_GLOBCONF))))
		goto end;
	if (0 != fmed_conf_fn(fn, 0))
		goto end;
	if (fn != filename)
		ffmem_free0(fn);

	r = 0;
end:
	ffmem_safefree(fn);
	return r;
}

static int fmed_conf_fn(const char *filename, uint flags)
{
	ffparser pconf;
	ffparser_schem ps;
	ffpars_ctx ctx = {0};
	int r = FFPARS_ESYS;
	ffstr s;
	char *buf = NULL;
	size_t n;
	fffd f = FF_BADFD;

	if (flags & CONF_F_USR) {
		ffpars_setargs(&ctx, &fmed->conf, fmed_confusr_args, FFCNT(fmed_confusr_args));
	} else {
		ffpars_setargs(&ctx, &fmed->conf, fmed_conf_args, FFCNT(fmed_conf_args));
	}

	ffconf_scheminit(&ps, &pconf, &ctx);
	if (FF_BADFD == (f = fffile_open(filename, O_RDONLY))) {
		if (fferr_nofile(fferr_last()) && (flags & CONF_F_OPT)) {
			r = 0;
			goto fail;
		}
		syserrlog(core, NULL, "core", "%s: %s", fffile_open_S, filename);
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
			, ps.p->line, ps.p->ch
			, &ps.p->val, (ps.curarg != NULL) ? ps.curarg->name : ""
			, (r == FFPARS_ESYS) ? fferr_strp(fferr_last()) : ser);
		goto fail;
	}

	r = 0;

fail:
	ffpars_free(&pconf);
	ffpars_schemfree(&ps);
	ffmem_safefree(buf);
	if (f != FF_BADFD)
		fffile_close(f);
	return r;
}


static void core_posted(void *udata)
{
}


static int cmd_init(fmed_cmd *cmd)
{
	cmd->vorbis_qual = -255;
	cmd->aac_qual = (uint)-1;
	cmd->mpeg_qual = 0xffff;
	cmd->flac_complevel = 0xff;

	cmd->lbdev_name = (uint)-1;
	cmd->volume = 100;
	cmd->cue_gaps = 255;
	return 0;
}

static void cmd_destroy(fmed_cmd *cmd)
{
	FFARR_FREE_ALL_PTR(&cmd->in_files, ffmem_free, char*);
	ffstr_free(&cmd->outfn);

	ffstr_free(&cmd->meta);
	ffmem_safefree(cmd->trackno);
	ffmem_safefree(cmd->conf_fn);

	ffmem_safefree(cmd->globcmd_pipename);
	ffstr_free(&cmd->globcmd);
}

static int conf_init(fmed_config *conf)
{
	conf->codepage = FFU_WIN1252;
	return 0;
}

static void conf_destroy(fmed_config *conf)
{
	ffarr_free(&conf->inmap);
	ffarr_free(&conf->outmap);
}


fmed_core* core_init(fmed_cmd **ptr, char **argv, char **env)
{
	ffmem_init();
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return NULL;
	fmed->cmd.log = &log_dummy;
	if (0 != ffenv_init(&fmed->env, env))
		goto err;

	fftime_zone tz;
	fftime_local(&tz);
	fftime_storelocal(&tz);
	fftime_init();

	fmed->kq = FF_BADFD;
	fftask_init(&fmed->taskmgr);
	fftmrq_init(&fmed->tmrq);
	fflist_init(&fmed->trks);
	fflist_init(&fmed->mods);
	core_insmod("#core.core", NULL);

	if (0 != cmd_init(&fmed->cmd)
		|| 0 != conf_init(&fmed->conf)) {
		goto err;
	}

	char fn[FF_MAXPATH];
	ffstr path;
	const char *p;
	if (NULL == (p = ffps_filename(fn, sizeof(fn), argv[0])))
		goto err;
	if (NULL == ffpath_split2(p, ffsz_len(p), &path, NULL))
		goto err;
	if (NULL == ffstr_copy(&fmed->root, path.ptr, path.len + FFSLEN("/")))
		goto err;

#ifdef FF_WIN
	{
	ffarr path = {0};
	if (0 == ffstr_catfmt(&path, "%Smod%Z", &fmed->root)) {
		ffarr_free(&path);
		goto err;
	}
	ffdl_init(path.ptr);
	ffarr_free(&path);
	}
#endif

	*ptr = &fmed->cmd;
	core->loglev = FMED_LOG_INFO;
	core->props = &fmed->props;
	return core;

err:
	core_free();
	return NULL;
}

void core_free(void)
{
	core_mod *mod;
	fflist_item *next;

	fftmrq_destroy(&fmed->tmrq, fmed->kq);

	_fmed_track.cmd(NULL, FMED_TRACK_STOPALL);

	if (fmed->kq != FF_BADFD) {
		ffkqu_post_detach(&fmed->kqpost, fmed->kq);
		ffkqu_close(fmed->kq);
	}

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		ffmem_free(mod);
	}

	core_modinfo *minfo;
	FFARR_WALKT(&fmed->bmods, minfo, core_modinfo) {
		mod_destroy(minfo);
	}
	ffarr_free(&fmed->bmods);

	cmd_destroy(&fmed->cmd);
	conf_destroy(&fmed->conf);
	ffstr_free(&fmed->root);
	ffenv_destroy(&fmed->env);

#ifdef FF_WIN
	FF_SAFECLOSE(fmed->woh, NULL, ffwoh_free);
#endif

	ffmem_free0(fmed);
}

static void mod_destroy(core_modinfo *m)
{
	ffmem_safefree(m->name);
	if (m->m != NULL)
		m->m->destroy();
	if (m->dl != NULL)
		ffdl_close(m->dl);
}

static core_modinfo* mod_load(const ffstr *ps)
{
	fmed_getmod_t getmod;
	core_modinfo *minfo, *rc = NULL;
	const fmed_mod *m;
	ffdl dl = NULL;
	ffstr s = *ps;
	ffarr a = {0};

	if (s.ptr[0] == '#') {
		if (ffstr_eqcz(&s, "#core"))
			getmod = &fmed_getmod_core;
		else if (ffstr_eqcz(&s, "#file"))
			getmod = &fmed_getmod_file;
		else if (ffstr_eqcz(&s, "#soundmod"))
			getmod = &fmed_getmod_sndmod;
		else if (ffstr_eqcz(&s, "#queue"))
			getmod = &fmed_getmod_queue;
		else if (ffstr_eqcz(&s, "#globcmd")) {
			getmod = &fmed_getmod_globcmd;
		} else {
			fferr_set(EINVAL);
			goto fail;
		}

	} else {
		if (0 == ffstr_catfmt(&a, "%Smod%c%S.%s%Z", &fmed->root, FFPATH_SLASH, &s, FFDL_EXT))
			goto fail;
		const char *fn = a.ptr;

		dl = ffdl_open(fn, FFDL_SELFDIR);
		if (dl == NULL) {
			errlog(core, NULL, "core", "module %s: %s: %s", fn, ffdl_open_S, ffdl_errstr());
			goto fail;
		}

		getmod = (void*)ffdl_addr(dl, FMED_MODFUNCNAME);
		if (getmod == NULL) {
			errlog(core, NULL, "core", "module %s: %s '%s': %s"
				, fn, ffdl_addr_S, FMED_MODFUNCNAME, ffdl_errstr());
			goto fail;
		}
	}

	if (NULL == (minfo = ffarr_pushgrowT(&fmed->bmods, 8, core_modinfo)))
		goto fail;
	ffmem_tzero(minfo);
	minfo->name = ffsz_alcopy(s.ptr, s.len);
	m = getmod(core);
	if (minfo->name == NULL || m == NULL)
		goto fail;

	if (m->ver_core != FMED_VER_CORE) {
		errlog(core, NULL, "core", "module %s v%u.%u: module is built for core v%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver)
			, FMED_VER_GETMAJ(m->ver_core), FMED_VER_GETMIN(m->ver_core));
		goto fail;
	}

	if (s.ptr[0] != '#') {
		dbglog(core, NULL, "core", "loaded module %s v%u.%u"
			, minfo->name, FMED_VER_GETMAJ(m->ver), FMED_VER_GETMIN(m->ver));
	}

	if (0 != m->sig(FMED_SIG_INIT))
		goto fail;

	minfo->m = m;
	minfo->dl = dl;
	rc = minfo;

fail:
	if (rc == NULL) {
		mod_destroy(minfo);
		fmed->bmods.len--;
		FF_SAFECLOSE(dl, NULL, ffdl_close);
	}
	ffarr_free(&a);
	return rc;
}

static const fmed_modinfo* core_insmod(const char *sname, ffpars_ctx *ctx)
{
	ffstr s, modname;
	size_t sname_len = ffsz_len(sname);
	core_mod *mod = ffmem_calloc(1, sizeof(core_mod) + sname_len + 1);
	if (mod == NULL)
		return NULL;

	ffs_split2by(sname, sname_len, '.', &s, &modname);
	if (s.len == 0 || modname.len == 0) {
		fferr_set(EINVAL);
		goto fail;
	}

	core_modinfo *minfo;
	if (NULL == (minfo = (void*)core_getmod2(FMED_MOD_SOINFO | FMED_MOD_NOLOG, s.ptr, s.len))) {
		if (NULL == (minfo = mod_load(&s)))
			goto fail;
	}

	mod->dl = minfo->dl;
	mod->m = minfo->m;
	if (NULL == (mod->iface = minfo->m->iface(modname.ptr))) {
		errlog(core, NULL, "core", "can't initialize %s", sname);
		goto fail;
	}

	ffsz_fcopy(mod->name_s, sname, sname_len);
	mod->name = mod->name_s;

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
	ffmem_free(mod);
	return NULL;
}

static core_modinfo* core_findmod(const ffstr *name)
{
	core_modinfo *mod;
	FFARR_WALKT(&fmed->bmods, mod, core_modinfo) {
		if (ffstr_eqz(name, mod->name))
			return mod;
	}
	return NULL;
}

static const void* core_getmod(const char *name)
{
	return core_getmod2(FMED_MOD_IFACE_ANY, name, -1);
}

static const fmed_modinfo* core_getmodinfo(const ffstr *name)
{
	core_mod *mod;
	FFLIST_WALK(&fmed->mods, mod, sib) {
		if (ffstr_eqz(name, mod->name))
			return (fmed_modinfo*)mod;
	}
	return NULL;
}

static const fmed_modinfo* core_modbyext(const ffstr3 *map, const ffstr *ext)
{
	const inmap_item *it = (void*)map->ptr;
	while (it != (void*)(map->ptr + map->len)) {
		size_t len = ffsz_len(it->ext);
		if (ffstr_ieq(ext, it->ext, len)) {
			return it->mod;
		}
		it = (void*)((char*)it + sizeof(inmap_item) + len + 1);
	}

	errlog(core, NULL, "core", "unknown file format: %S", ext);
	return NULL;
}

const void* core_getmod2(uint flags, const char *name, ssize_t name_len)
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
		return core_modbyext(&fmed->conf.inmap, &s);

	case FMED_MOD_OUTEXT:
		return core_modbyext(&fmed->conf.outmap, &s);

	case FMED_MOD_INFO:
		if (NULL == (mod = core_getmodinfo(&s)))
			goto err;
		return mod;

	case FMED_MOD_IFACE:
		if (NULL == (mod = core_getmodinfo(&s)))
			goto err;
		return mod->iface;

	case FMED_MOD_SOINFO:
	case FMED_MOD_IFACE_ANY: {
		core_modinfo *mi;
		const void *fc;
		ffs_split2by(s.ptr, s.len, '.', &smod, &iface);
		if (NULL == (mi = (void*)core_findmod(&smod)))
			goto err;
		if (t == FMED_MOD_SOINFO)
			return mi;
		if (iface.len == 0)
			goto err;
		if (NULL == (fc = mi->m->iface(iface.ptr)))
			goto err;
		return fc;
	}

	case FMED_MOD_INFO_ADEV_IN:
		return fmed->conf.input;
	case FMED_MOD_INFO_ADEV_OUT:
		return fmed->conf.output;
	}

err:
	if (!(flags & FMED_MOD_NOLOG))
		errlog(core, NULL, "core", "module not found: %S", &s);
	return NULL;
}

static int core_open(void)
{
#if defined FF_WIN && FF_WIN < 0x0600
	ffkqu_init();
#endif
	if (FF_BADFD == (fmed->kq = ffkqu_create())) {
		syserrlog(core, NULL, "core", "%s", ffkqu_create_S);
		return 1;
	}
	core->kq = fmed->kq;
	ffkqu_post_attach(&fmed->kqpost, fmed->kq);

	ffkqu_settm(&fmed->kqutime, (uint)-1);
	ffkev_init(&fmed->evposted);
	fmed->evposted.oneshot = 0;
	fmed->evposted.handler = &core_posted;

	fmed->qu = core->getmod("#queue.queue");
	return 0;
}

void core_work(void)
{
	ffkqu_entry *ents = ffmem_callocT(FMED_KQ_EVS, ffkqu_entry);
	if (ents == NULL)
		return;

	while (!fmed->stopped) {

		uint nevents = ffkqu_wait(fmed->kq, ents, FMED_KQ_EVS, &fmed->kqutime);

		if ((int)nevents < 0) {
			if (fferr_last() != EINTR) {
				syserrlog(core, NULL, "core", "%s", ffkqu_wait_S);
				break;
			}
			continue;
		}

		for (uint i = 0;  i != nevents;  i++) {
			ffkqu_entry *ev = &ents[i];
			ffkev_call(ev);

			fftask_run(&fmed->taskmgr);
		}
	}

	ffmem_free(ents);
}

static char* core_getpath(const char *name, size_t len)
{
	ffstr3 s = {0};
	if (0 == ffstr_catfmt(&s, "%S%*s%Z", &fmed->root, len, name)) {
		ffarr_free(&s);
		return NULL;
	}
	return s.ptr;
}

static char* core_env_expand(char *dst, size_t cap, const char *src)
{
	return ffenv_expand(&fmed->env, dst, cap, src);
}

static int core_sigmods(uint signo)
{
	core_modinfo *mod;
	FFARR_WALKT(&fmed->bmods, mod, core_modinfo) {
		if (0 != mod->m->sig(signo))
			return 1;
	}
	return 0;
}

static int core_filetype(const char *fn)
{
	ffstr ext;
	fffileinfo fi;

	if (0 == fffile_infofn(fn, &fi))
		if (fffile_isdir(fffile_infoattr(&fi)))
			return FMED_FT_DIR;

	ffpath_split3(fn, ffsz_len(fn), NULL, NULL, &ext);
	if (ffstr_eqcz(&ext, "m3u8")
		|| ffstr_eqcz(&ext, "m3u")
		|| ffstr_eqcz(&ext, "pls"))
		return FMED_FT_PLIST;

	if (NULL != core_getmod2(FMED_MOD_INEXT, ext.ptr, ext.len))
		return FMED_FT_FILE;

	return FMED_FT_UKN;
}

static ssize_t core_cmd(uint signo, ...)
{
	ssize_t r = 0;
	va_list va;
	va_start(va, signo);

	dbglog(core, NULL, "core", "received signal: %u", signo);

	switch (signo) {

	case FMED_CONF: {
		const char *fn = va_arg(va, char*);
		if (0 != fmed_conf(fn)) {
			r = 1;
		}
		break;
	}

	case FMED_OPEN:
		if (0 != core_open()) {
			r = 1;
			break;
		}
		if (0 != core_sigmods(signo)) {
			r = 1;
			break;
		}
		break;

	case FMED_START:
		core_work();
		break;

	case FMED_STOP:
		core_sigmods(signo);
		fmed->stopped = 1;
		ffkqu_post(&fmed->kqpost, &fmed->evposted);
		break;

	case FMED_FILETYPE:
		r = core_filetype(va_arg(va, char*));
		break;

#ifdef FF_WIN
	case FMED_WOH_INIT:
		if (fmed->woh == NULL)
			if (NULL == (fmed->woh = ffwoh_create())) {
				syswarnlog(NULL, "ffwoh_create()");
				r = -1;
			}
		break;

	case FMED_WOH_ADD: {
		HANDLE h = va_arg(va, HANDLE);
		fftask *t = va_arg(va, fftask*);
		if (0 != (r = ffwoh_add(fmed->woh, h, t->handler, t->param)))
			syswarnlog(NULL, "ffwoh_add()");
		break;
	}
	case FMED_WOH_DEL: {
		HANDLE h = va_arg(va, HANDLE);
		ffwoh_rm(fmed->woh, h);
		r = 0;
		break;
	}
#endif
	}

	va_end(va);
	return r;
}

static int core_sig(uint signo)
{
	return core_cmd(signo);
}


static void core_task(fftask *task, uint cmd)
{
	dbglog(core, NULL, "core", "task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, cmd, fftask_active(&fmed->taskmgr, task), task->handler, task->param);

	switch (cmd) {
	case FMED_TASK_POST:
		if (1 == fftask_post(&fmed->taskmgr, task))
			ffkqu_post(&fmed->kqpost, &fmed->evposted);
		break;
	case FMED_TASK_DEL:
		fftask_del(&fmed->taskmgr, task);
		break;
	default:
		FF_ASSERT(0);
	}
}

static int core_timer(fftmrq_entry *tmr, int64 _interval, uint flags)
{
	int interval = _interval;
	uint period = ffmin((uint)ffabs(interval), TMR_INT);
	dbglog(core, NULL, "core", "timer:%p  interval:%d  handler:%p  param:%p"
		, tmr, interval, tmr->handler, tmr->param);

	if (fftmrq_active(&fmed->tmrq, tmr))
		fftmrq_rm(&fmed->tmrq, tmr);
	else if (interval == 0)
		return 0;

	if (interval == 0) {
		if (fftmrq_empty(&fmed->tmrq)) {
			fftmrq_stop(&fmed->tmrq, fmed->kq);
			dbglog(core, NULL, "core", "stopped kernel timer", 0);
		}
		return 0;
	}

	if (fftmrq_started(&fmed->tmrq) && period < fmed->period) {
		fftmrq_stop(&fmed->tmrq, fmed->kq);
		dbglog(core, NULL, "core", "restarting kernel timer", 0);
	}

	if (!fftmrq_started(&fmed->tmrq)) {
		if (0 != fftmrq_start(&fmed->tmrq, fmed->kq, period)) {
			syserrlog(core, NULL, "core", "fftmrq_start()", 0);
			return -1;
		}
		fmed->period = period;
		dbglog(core, NULL, "core", "started kernel timer  interval:%u", period);
	}

	fftmrq_add(&fmed->tmrq, tmr, interval);
	return 0;
}

static int64 core_getval(const char *name)
{
	if (!ffsz_cmp(name, "repeat_all"))
		return fmed->cmd.repeat_all;
	else if (!ffsz_cmp(name, "codepage"))
		return fmed->conf.codepage;
	else if (!ffsz_cmp(name, "gui"))
		return fmed->cmd.gui;
	else if (!ffsz_cmp(name, "next_if_error"))
		return fmed->cmd.notui;
	else if (!ffsz_cmp(name, "show_tags"))
		return fmed->cmd.tags;
	else if (!ffsz_cmp(name, "cue_gaps") && fmed->cmd.cue_gaps != 255)
		return fmed->cmd.cue_gaps;
	else if (!ffsz_cmp(name, "instance_mode"))
		return fmed->conf.instance_mode;
	return FMED_NULL;
}

static const char *const loglevs[] = {
	"error", "warning", "info", "info", "debug",
};

static void core_log(uint flags, void *trk, const char *module, const char *fmt, ...)
{
	char stime[32];
	ffdtm dt;
	fftime t;
	size_t r;
	fmed_logdata ld;
	uint lev = flags & _FMED_LOG_LEVMASK;
	int e;

	if (flags & FMED_LOG_SYS)
		e = fferr_last();

	fftime_now(&t);
	fftime_split(&dt, &t, FFTIME_TZLOCAL);
	r = fftime_tostr(&dt, stime, sizeof(stime), FFTIME_HMS_MSEC);
	stime[r] = '\0';
	ld.stime = stime;

	FF_ASSERT(lev != 0);
	ld.level = loglevs[lev - 1];

	ld.ctx = NULL;
	ld.module = module;
	if (trk != NULL) {
		_fmed_track.loginfo(trk, &ld.ctx, &module);
		if (ld.module == NULL)
			ld.module = module;
	}

	if (flags & FMED_LOG_SYS)
		fferr_set(e);

	ld.fmt = fmt;
	va_start(ld.va, fmt);
	fmed->cmd.log->log(flags, &ld);
	va_end(ld.va);
}


static const fmed_mod* fmed_getmod_core(const fmed_core *_core)
{
	return &fmed_core_mod;
}

static const void* core_iface(const char *name)
{
	if (!ffsz_cmp(name, "core"))
		return (void*)1;
	else if (!ffsz_cmp(name, "track"))
		return &_fmed_track;
	return NULL;
}

static int core_sig2(uint signo)
{
	return 0;
}

static void core_destroy(void)
{

}
