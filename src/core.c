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
#include <FFOS/thread.h>


#undef syserrlog
#define dbglog0(...)  fmed_dbglog(core, NULL, "core", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "core", __VA_ARGS__)
#define syswarnlog(trk, ...)  fmed_syswarnlog(core, NULL, "core", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "core", __VA_ARGS__)


#define FMED_ASSERT(expr) \
while (!(expr)) { \
	FF_ASSERT(0); \
	ffps_exit(1); \
}


struct worker {
	ffthd thd;
	ffthd_id id;
	fffd kq;

	fftaskmgr taskmgr;
	ffkevpost kqpost;
	ffkevent evposted;

	fftimer_queue tmrq;
	uint period;

	ffatomic njobs;
	uint init :1;
};

typedef struct inmap_item {
	const fmed_modinfo *mod;
	char ext[0];
} inmap_item;

typedef struct core_modinfo {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	void *iface; //dummy

	uint opened :1;
} core_modinfo;

typedef struct core_mod {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const void *iface;

	ffpars_ctx conf_ctx;
	ffstr conf_data;
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

extern int tracks_init(void);
extern void tracks_destroy(void);

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_globcmd(const fmed_core *_core);

static int core_open(void);
static int core_sigmods(uint signo);
static core_modinfo* core_findmod(const ffstr *name);
static const fmed_modinfo* core_insmod_delayed(const char *sname, ffpars_ctx *ctx);
static int mod_readconf(core_mod *mod, const char *name);
static int mod_load_delayed(core_mod *mod);
static const fmed_modinfo* core_getmodinfo(const ffstr *name);
static const fmed_modinfo* core_modbyext(const ffstr3 *map, const ffstr *ext);
static int core_filetype(const char *fn);

static core_modinfo* mod_create(const ffstr *soname);
static int mod_load(core_modinfo *minfo);
static void mod_destroy(core_modinfo *m);

static core_mod* mod_createiface(const ffstr *name);
static int mod_loadiface(core_mod *mod, const core_modinfo *bmod);
static void mod_freeiface(core_mod *m);

static int wrk_init(struct worker *w, uint thread);
static void wrk_destroy(struct worker *w);
static uint work_assign(uint flags);
static void work_release(uint wid, uint flags);
static uint work_avail();
static int FFTHDCALL core_work(void *param);

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
static const void* core_getmod2(uint flags, const char *name, ssize_t name_len);
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
	{ "workers",  FFPARS_TINT8, FFPARS_DSTOFF(fmed_config, workers) },
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
	{ "prevent_sleep",  FFPARS_TBOOL8, FFPARS_DSTOFF(fmed_config, prevent_sleep) },
	{ "include",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_include) },
	{ "include_user",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_include) },
};

static int fmed_confusr_mod(ffparser_schem *ps, void *obj, ffstr *val);

static const ffpars_arg fmed_confusr_args[] = {
	{ "*",	FFPARS_TSTR | FFPARS_FMULTI, FFPARS_DST(&fmed_confusr_mod) },
};

#define MODS_WIN_ONLY  "wasapi.", "direct-sound."
#define MODS_LINUX_ONLY  "alsa.", "pulse."
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
	if (ffstr_matchz(name, "gui.") && !fmed->cmd.gui)
		return 0;
	if (ffstr_matchz(name, "tui.") && (fmed->cmd.notui || fmed->cmd.gui))
		return 0;
	const char *const *s;
	FFARRS_FOREACH(mods_skip, s) {
		if (ffstr_matchz(name, *s))
			return 0;
	}
	return 1;
}

static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	if (!allowed_mod(val))
		goto end;

	if (NULL == core_insmod_delayed(val->ptr, NULL))
		return FFPARS_ESYS;

end:
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

	zname = ffsz_alcopy(name->ptr, name->len);
	if (zname == NULL)
		return FFPARS_ESYS;

	const fmed_modinfo *m;
	if (ffstr_matchz(name, "tui.")
		|| ffstr_matchz(name, "gui.")
		|| fmed->conf_copy_mod != NULL) {
		// UI module must load immediately
		// Note: delayed modules loading from "include" config directive isn't supported
		m = core_insmod(zname, ctx);
	} else {
		m = core_insmod_delayed(zname, ctx);
		if (m != NULL && zname[0] != '#') {
			ffconf_ctxcopy_init(&fmed->conf_copy, p);
			fmed->conf_copy_mod = (void*)m;
		}
	}

	if (m == NULL) {
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
	ffconf *conf = p->p;

	if (conf->type == FFCONF_TKEY) {
		const fmed_modinfo *mod = core_getmodinfo(val);
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
	ffconf *pconf = ps->p;

	if (conf->skip_line) {
		if (pconf->type == FFCONF_TVAL) {
			conf->skip_line = 0;
		}
		return 0;
	}

	if (pconf->type != FFCONF_TKEYCTX)
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
	ffconf pconf;
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

			if (fmed->conf_copy_mod != NULL) {
				int r2 = ffconf_ctx_copy(&fmed->conf_copy, &pconf);
				if (r2 < 0) {
					errlog0("parse config: %s: %u:%u: ffconf_ctx_copy()"
						, filename
						, pconf.line, pconf.ch);
					goto fail;
				} else if (r2 > 0) {
					core_mod *m = (void*)fmed->conf_copy_mod;
					m->conf_data = ffconf_ctxcopy_acquire(&fmed->conf_copy);
					fmed->conf_copy_mod = NULL;
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
	fmed->conf_copy_mod = NULL;
	ffconf_ctxcopy_destroy(&fmed->conf_copy);
	ffconf_parseclose(&pconf);
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
	ffmem_safefree(cmd->aac_profile);
	ffmem_safefree(cmd->trackno);
	ffmem_safefree(cmd->conf_fn);

	ffmem_safefree(cmd->globcmd_pipename);
	ffstr_free(&cmd->globcmd);
	ffarr2_free(&cmd->include_files);
	ffarr2_free(&cmd->exclude_files);
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

	fflist_init(&fmed->mods);
	core_insmod("#core.core", NULL);

	ffkqu_settm(&fmed->kqutime, (uint)-1);

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
	fmed->props.version_str = FMED_VER;
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

	struct worker *w;
	FFARR_WALKT(&fmed->workers, w, struct worker) {
		if (w->init)
			wrk_destroy(w);
	}

	tracks_destroy();

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		mod_freeiface(mod);
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

/** Get pointer for a new module. */
static core_modinfo* mod_create(const ffstr *soname)
{
	core_modinfo *bmod;
	if (NULL == (bmod = ffarr_pushgrowT(&fmed->bmods, 8, core_modinfo)))
		goto fail;
	ffmem_tzero(bmod);
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
	ffarr a = {0};

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

	m = getmod(core);
	if (m == NULL)
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
	rc = 0;

fail:
	if (rc != 0) {
		FF_SAFECLOSE(dl, NULL, ffdl_close);
	}
	ffarr_free(&a);
	return rc;
}

/** Get pointer for a new module interface. */
static core_mod* mod_createiface(const ffstr *name)
{
	core_mod *mod;
	if (NULL == (mod = ffmem_calloc(1, sizeof(core_mod) + name->len + 1)))
		return NULL;
	fflist_ins(&fmed->mods, &mod->sib);
	ffsz_fcopy(mod->name_s, name->ptr, name->len);
	mod->name = mod->name_s;
	return mod;
}

static void mod_freeiface(core_mod *m)
{
	ffstr_free(&m->conf_data);
	ffmem_free(m);
}

/** Load module interface. */
static int mod_loadiface(core_mod *mod, const core_modinfo *bmod)
{
	ffstr soname, modname;
	ffs_split2by(mod->name, ffsz_len(mod->name), '.', &soname, &modname);

	if (NULL == (mod->iface = bmod->m->iface(modname.ptr))) {
		errlog(core, NULL, "core", "can't initialize %s", mod->name);
		goto end;
	}

	mod->dl = bmod->dl;
	mod->m = bmod->m;
	return 0;

end:
	return -1;
}

/** Enlist a new module which will be loaded later. */
static const fmed_modinfo* core_insmod_delayed(const char *sname, ffpars_ctx *ctx)
{
	core_mod *mod = NULL;
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

	return (void*)mod;

fail:
	if (mod != NULL) {
		fflist_rm(&fmed->mods, &mod->sib);
		mod_freeiface(mod);
	}
	if (bmod != NULL) {
		mod_destroy(bmod);
		fmed->bmods.len--;
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

	return (fmed_modinfo*)mod;

fail:
	if (mod != NULL) {
		fflist_rm(&fmed->mods, &mod->sib);
		mod_freeiface(mod);
	}
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
	const void *m = core_getmod2(FMED_MOD_IFACE | FMED_MOD_NOLOG, name, -1);
	if (m != NULL)
		return m;
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
	errlog0("can't load module %s: config parser: %s"
		, mod->name, ffpars_errstr(r));
	ffconf_parseclose(&conf);
	ffpars_schemfree(&ps);
	return -1;
}

/** Actually load a module. */
static int mod_load_delayed(core_mod *mod)
{
	ffstr soname, modname;
	ffs_split2by(mod->name, ffsz_len(mod->name), '.', &soname, &modname);

	core_modinfo *bmod;
	if (NULL == (bmod = core_findmod(&soname)))
		goto fail;
	if (bmod->m == NULL && 0 != mod_load(bmod))
		goto fail;

	if (0 != mod_loadiface(mod, bmod))
		goto fail;

	if (mod->conf_data.len != 0) {
		if (0 != mod_readconf(mod, modname.ptr))
			goto fail;
	}

	if (!bmod->opened) {
		dbglog0("signal:%u for module %s", (int)FMED_OPEN, bmod->name);
		if (0 != bmod->m->sig(FMED_OPEN))
			goto fail;
		bmod->opened = 1;
	}

	return 0;

fail:
	errlog0("can't load module %s", mod->name);
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
		mod = core_modbyext(&fmed->conf.inmap, &s);
		break;

	case FMED_MOD_OUTEXT:
		mod = core_modbyext(&fmed->conf.outmap, &s);
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
	}

	if (mod == NULL)
		goto err;
	if (mod->m == NULL && 0 != mod_load_delayed((core_mod*)mod))
		return NULL;
	return mod;

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
	uint n = fmed->conf.workers;
	if (n == 0) {
		ffsysconf sc;
		ffsc_init(&sc);
		n = ffsc_get(&sc, _SC_NPROCESSORS_ONLN);
	}
	if (NULL == ffarr_alloczT(&fmed->workers, n, struct worker))
		return 1;
	fmed->workers.len = n;
	struct worker *w = (void*)fmed->workers.ptr;
	if (0 != wrk_init(w, 0))
		return 1;
	core->kq = w->kq;

	fmed->qu = core->getmod("#queue.queue");
	if (0 != tracks_init())
		return 1;
	return 0;
}

static int wrk_init(struct worker *w, uint thread)
{
	fftask_init(&w->taskmgr);
	fftmrq_init(&w->tmrq);

	if (FF_BADFD == (w->kq = ffkqu_create())) {
		syserrlog("%s", ffkqu_create_S);
		return 1;
	}
	ffkqu_post_attach(&w->kqpost, w->kq);

	ffkev_init(&w->evposted);
	w->evposted.oneshot = 0;
	w->evposted.handler = &core_posted;

	if (thread) {
		w->thd = ffthd_create(&core_work, w, 0);
		if (w->thd == FFTHD_INV) {
			syserrlog("%s", ffthd_create_S);
			wrk_destroy(w);
			return 1;
		}
		// w->id is set inside a new thread
	} else {
		w->id = ffthd_curid();
	}

	w->init = 1;
	return 0;
}

static void wrk_destroy(struct worker *w)
{
	if (w->thd != FFTHD_INV) {
		ffthd_join(w->thd, -1, NULL);
		dbglog(core, NULL, "core", "thread %xU exited", w->id);
		w->thd = FFTHD_INV;
	}
	fftmrq_destroy(&w->tmrq, w->kq);
	if (w->kq != FF_BADFD) {
		ffkqu_post_detach(&w->kqpost, w->kq);
		ffkqu_close(w->kq);
		w->kq = FF_BADFD;
	}
}

/** Find the worker with the least number of active jobs.
Initialize data and create a thread if necessary.
Return worker ID. */
static uint work_assign(uint flags)
{
	struct worker *w, *ww = (void*)fmed->workers.ptr;
	uint id = 0, j = -1;

	if (flags == 0) {
		id = 0;
		w = &ww[0];
		goto done;
	}

	FFARR_WALKT(&fmed->workers, w, struct worker) {
		uint nj = ffatom_get(&w->njobs);
		if (nj < j) {
			id = w - ww;
			j = nj;
			if (nj == 0)
				break;
		}
	}
	w = &ww[id];

	if (!w->init
		&& 0 != wrk_init(w, 1)) {
		id = 0;
		w = &ww[0];
		goto done;
	}

done:
	ffatom_inc(&w->njobs);
	return id;
}

/** A job is completed. */
static void work_release(uint wid, uint flags)
{
	struct worker *w = ffarr_itemT(&fmed->workers, wid, struct worker);
	ssize_t n = ffatom_decret(&w->njobs);
	FMED_ASSERT(n >= 0);
}

/** Get the number of available workers. */
static uint work_avail()
{
	struct worker *w;
	FFARR_WALKT(&fmed->workers, w, struct worker) {
		if (ffatom_get(&w->njobs) == 0)
			return 1;
	}
	return 0;
}

void core_job_enter(uint id, size_t *ctx)
{
	struct worker *w = ffarr_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	*ctx = w->taskmgr.tasks.len;
}

ffbool core_job_shouldyield(uint id, size_t *ctx)
{
	struct worker *w = ffarr_itemT(&fmed->workers, id, struct worker);
	FF_ASSERT(w->id == ffthd_curid());
	return (*ctx != w->taskmgr.tasks.len);
}

ffbool core_ismainthr(void)
{
	struct worker *w = ffarr_itemT(&fmed->workers, 0, struct worker);
	return (w->id == ffthd_curid());
}

static int FFTHDCALL core_work(void *param)
{
	struct worker *w = param;
	w->id = ffthd_curid();
	ffkqu_entry *ents = ffmem_callocT(FMED_KQ_EVS, ffkqu_entry);
	if (ents == NULL)
		return -1;

	dbglog(core, NULL, "core", "entering kqueue loop", 0);

	while (!fmed->stopped) {

		uint nevents = ffkqu_wait(w->kq, ents, FMED_KQ_EVS, &fmed->kqutime);

		if ((int)nevents < 0) {
			if (fferr_last() != EINTR) {
				syserrlog("%s", ffkqu_wait_S);
				break;
			}
			continue;
		}

		for (uint i = 0;  i != nevents;  i++) {
			ffkqu_entry *ev = &ents[i];
			ffkev_call(ev);

			fftask_run(&w->taskmgr);
		}
	}

	ffmem_free(ents);
	return 0;
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
		if (mod->m == NULL)
			continue;
		if (0 != mod->m->sig(signo))
			return 1;
		if (signo == FMED_OPEN)
			mod->opened = 1;
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
		core_work(fmed->workers.ptr);
		break;

	case FMED_STOP: {
		FF_ASSERT(core_ismainthr());
		core_sigmods(signo);
		fmed->stopped = 1;
		struct worker *w;
		FFARR_WALKT(&fmed->workers, w, struct worker) {
			ffkqu_post(&w->kqpost, &w->evposted);
		}
		break;
	}

	case FMED_FILETYPE:
		r = core_filetype(va_arg(va, char*));
		break;

	case FMED_TASK_XPOST:
	case FMED_TASK_XDEL: {
		fftask *task = va_arg(va, fftask*);
		uint wid = va_arg(va, uint);
		struct worker *w = (void*)fmed->workers.ptr;
		FF_ASSERT(wid < fmed->workers.len);
		if (wid >= fmed->workers.len) {
			r = -1;
			break;
		}
		w = &w[wid];

		dbglog(core, NULL, "core", "task:%p, cmd:%u, active:%u, handler:%p, param:%p"
			, task, signo, fftask_active(&w->taskmgr, task), task->handler, task->param);

		if (signo == FMED_TASK_XPOST) {
			if (1 == fftask_post(&w->taskmgr, task))
				ffkqu_post(&w->kqpost, &w->evposted);
		} else
			fftask_del(&w->taskmgr, task);
		break;
	}

	case FMED_WORKER_ASSIGN: {
		fffd *pkq = va_arg(va, fffd*);
		uint flags = va_arg(va, uint);
		r = work_assign(flags);
		struct worker *w = ffarr_itemT(&fmed->workers, r, struct worker);
		*pkq = w->kq;
		break;
	}

	case FMED_WORKER_RELEASE: {
		uint wid = va_arg(va, uint);
		uint flags = va_arg(va, uint);
		work_release(wid, flags);
		break;
	}

	case FMED_WORKER_AVAIL:
		r = work_avail();
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
	struct worker *w = (void*)fmed->workers.ptr;

	dbglog(core, NULL, "core", "task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, cmd, fftask_active(&w->taskmgr, task), task->handler, task->param);

	switch (cmd) {
	case FMED_TASK_POST:
		if (1 == fftask_post(&w->taskmgr, task))
			ffkqu_post(&w->kqpost, &w->evposted);
		break;
	case FMED_TASK_DEL:
		fftask_del(&w->taskmgr, task);
		break;
	default:
		FF_ASSERT(0);
	}
}

static int core_timer(fftmrq_entry *tmr, int64 _interval, uint flags)
{
	struct worker *w = (void*)fmed->workers.ptr;
	int interval = _interval;
	uint period = ffmin((uint)ffabs(interval), TMR_INT);
	dbglog(core, NULL, "core", "timer:%p  interval:%d  handler:%p  param:%p"
		, tmr, interval, tmr->handler, tmr->param);

	if (w->kq == FF_BADFD) {
		dbglog0("timer's not ready", 0);
		return -1;
	}

	if (fftmrq_active(&w->tmrq, tmr))
		fftmrq_rm(&w->tmrq, tmr);
	else if (interval == 0)
		return 0;

	if (interval == 0) {
		if (fftmrq_empty(&w->tmrq)) {
			fftmrq_stop(&w->tmrq, w->kq);
			dbglog(core, NULL, "core", "stopped kernel timer", 0);
		}
		return 0;
	}

	if (fftmrq_started(&w->tmrq) && period < w->period) {
		fftmrq_stop(&w->tmrq, w->kq);
		dbglog(core, NULL, "core", "restarting kernel timer", 0);
	}

	if (!fftmrq_started(&w->tmrq)) {
		if (0 != fftmrq_start(&w->tmrq, w->kq, period)) {
			syserrlog("%s", "fftmrq_start()");
			return -1;
		}
		w->period = period;
		dbglog(core, NULL, "core", "started kernel timer  interval:%u", period);
	}

	fftmrq_add(&w->tmrq, tmr, interval);
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
	ld.tid = ffthd_curid();

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
