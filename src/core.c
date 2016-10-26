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
	const fmed_filter *f;

	fflist_item sib;
	char name_s[0];
} core_mod;


fmedia *fmed;

enum {
	FMED_KQ_EVS = 100,
	CONF_MBUF = 4096,
};

#ifdef FF_UNIX
#define USR_CONF  "$HOME/.config/fmedia/fmedia.conf"
#else
#define USR_CONF  "%APPDATA%/fmedia/fmedia.conf"
#endif


FF_EXP fmed_core* core_init(fmed_cmd **ptr);
FF_EXP void core_free(void);

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_tui(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);

static int core_open(void);
static int core_sigmods(uint signo);
static core_modinfo* core_findmod(const ffstr *name);
static const fmed_modinfo* core_getmodinfo(const ffstr *name);

static const void* core_iface(const char *name);
static int core_sig2(uint signo);
static void core_destroy(void);
static const fmed_mod fmed_core_mod = {
	&core_iface, &core_sig2, &core_destroy
};

// CORE
static int64 core_getval(const char *name);
static void core_log(uint flags, void *trk, const char *module, const char *fmt, ...);
static char* core_getpath(const char *name, size_t len);
static int core_sig(uint signo);
static const void* core_getmod(const char *name);
static const fmed_modinfo* core_insmod(const char *name, ffpars_ctx *ctx);
static void core_task(fftask *task, uint cmd);
static fmed_core _fmed_core = {
	0, 0,
	&core_getval,
	&core_log,
	&core_getpath,
	&core_sig,
	&core_getmod, &core_insmod,
	&core_task,
};
fmed_core *core = &_fmed_core;

//LOG
static void log_dummy_func(uint flags, fmed_logdata *ld)
{
}
static const fmed_log log_dummy = {
	&log_dummy_func
};

static int fmed_conf(uint userconf);
static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_setmod(const fmed_modinfo **pmod, ffstr *val);
static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_input(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_recfmt(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_inp_channels(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_ext_val(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_codepage(ffparser_schem *p, void *obj, ffstr *val);

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
};

static int fmed_confusr_mod(ffparser_schem *ps, void *obj, ffpars_ctx *ctx);

static const ffpars_arg fmed_confusr_args[] = {
	{ "*",	FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FMULTI, FFPARS_DST(&fmed_confusr_mod) },
};


static int allowed_mod(const ffstr *name)
{
#ifdef FF_WIN
	if (ffstr_matchcz(name, "alsa."))
#else
	if (ffstr_matchcz(name, "wasapi.")
		|| ffstr_matchcz(name, "direct-sound."))
#endif
		return 0;
	return 1;
}

static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	if (ffstr_eqcz(val, "#tui.tui") && (fmed->cmd.notui || fmed->cmd.gui))
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

static int fmed_conf_setmod(const fmed_modinfo **pmod, ffstr *val)
{
	if (NULL == (*pmod = core_getmodinfo(val))) {
		return FFPARS_EBADVAL;
	}
	return 0;
}

static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val))
		return 0;

	return fmed_conf_setmod(&conf->output, val);
}

static int fmed_conf_input(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;

	if (!allowed_mod(val))
		return 0;

	return fmed_conf_setmod(&conf->input, val);
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

/** Process "so.modname" part from "so.modname.key value" */
static int fmed_confusr_mod(ffparser_schem *ps, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	ffstr *val = &ps->vals[0];
	const fmed_modinfo *mod;

	if (ps->p->type != FFCONF_TKEYCTX)
		return FFPARS_EUKNKEY;

	if (ffstr_eqcz(val, "core"))
		ffpars_setargs(ctx, conf, fmed_conf_args, FFCNT(fmed_conf_args));

	else if (conf->usrconf_modname == NULL) {
		if (NULL == (conf->usrconf_modname = ffsz_alcopy(val->ptr, val->len)))
			return FFPARS_ESYS;
		ffpars_setargs(ctx, &fmed->conf, fmed_confusr_args, FFCNT(fmed_confusr_args));

	} else {
		ffstr3 s = {0};
		if (0 == ffstr_catfmt(&s, "%s.%S", conf->usrconf_modname, val))
			return FFPARS_ESYS;
		mod = core_getmodinfo((ffstr*)&s);

		ffmem_free(conf->usrconf_modname);
		conf->usrconf_modname = NULL;
		ffarr_free(&s);

		if (mod == NULL || mod->f->conf == NULL)
			return FFPARS_EINTL;
		mod->f->conf(ctx);
	}

	return 0;
}

static int fmed_conf(uint userconf)
{
	ffparser pconf;
	ffparser_schem ps;
	ffpars_ctx ctx = {0};
	int r = FFPARS_ESYS;
	ffstr s;
	char *buf = NULL;
	size_t n;
	fffd f = FF_BADFD;
	char *filename;

	if (userconf == 1) {
		ffpars_setargs(&ctx, &fmed->conf, fmed_confusr_args, FFCNT(fmed_confusr_args));
		ffconf_scheminit(&ps, &pconf, &ctx);

		if (NULL == (filename = ffenv_expand(NULL, 0, USR_CONF)))
			return 0;

		if (FF_BADFD == (f = fffile_open(filename, O_RDONLY))) {
			if (!fferr_nofile(fferr_last()))
				syserrlog(core, NULL, "core", "%e: %s", FFERR_FOPEN, filename);
			r = 0;
			goto fail;
		}

	} else {

		ffpars_setargs(&ctx, &fmed->conf, fmed_conf_args, FFCNT(fmed_conf_args));
		ffconf_scheminit(&ps, &pconf, &ctx);

		if (NULL == (filename = core->getpath(FFSTR("fmedia.conf"))))
			return -1;

		if (FF_BADFD == (f = fffile_open(filename, O_RDONLY))) {
			goto err;
		}
	}

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
			n = s.len;
			r = ffconf_parse(&pconf, s.ptr, &n);
			ffstr_shift(&s, n);
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
	ffmem_free(filename);
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
	cmd->ogg_qual = -255;
	cmd->aac_qual = (uint)-1;
	cmd->mpeg_qual = 0xffff;
	cmd->flac_complevel = 0xff;

	cmd->volume = 100;
	cmd->cue_gaps = 255;
	if (NULL == ffstr_copy(&cmd->outdir, FFSTR(".")))
		return -1;
	return 0;
}

static void cmd_destroy(fmed_cmd *cmd)
{
	uint i;
	char **fn;

	fn = (char**)cmd->in_files.ptr;
	for (i = 0;  i < cmd->in_files.len;  i++, fn++) {
		ffmem_free(*fn);
	}
	ffarr_free(&cmd->in_files);
	ffstr_free(&cmd->outfn);
	ffstr_free(&cmd->outdir);

	ffstr_free(&cmd->meta);
	ffmem_safefree(cmd->trackno);

	ffstr_free(&cmd->root);
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


fmed_core* core_init(fmed_cmd **ptr)
{
	ffmem_init();
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return NULL;
	fmed->cmd.log = &log_dummy;

	fmed->kq = FF_BADFD;
	fftask_init(&fmed->taskmgr);
	fflist_init(&fmed->trks);
	fflist_init(&fmed->mods);
	core_insmod("#core.core", NULL);

	if (0 != cmd_init(&fmed->cmd)
		|| 0 != conf_init(&fmed->conf)) {
		core_free();
		return NULL;
	}

	*ptr = &fmed->cmd;
	return core;
}

void core_free(void)
{
	core_mod *mod;
	fflist_item *next;

	_fmed_track.cmd(NULL, FMED_TRACK_STOPALL);

	if (fmed->kq != FF_BADFD) {
		ffkqu_post_attach(FF_BADFD);
		ffkqu_close(fmed->kq);
	}

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		ffmem_free(mod);
	}

	core_modinfo *minfo;
	FFARR_WALKT(&fmed->bmods, minfo, core_modinfo) {
		ffmem_safefree(minfo->name);
		if (minfo->m != NULL)
			minfo->m->destroy();
		if (minfo->dl != NULL)
			ffdl_close(minfo->dl);
	}
	ffarr_free(&fmed->bmods);

	cmd_destroy(&fmed->cmd);
	conf_destroy(&fmed->conf);

	ffmem_free(fmed);
	fmed = NULL;
}

static const fmed_modinfo* core_insmod(const char *sname, ffpars_ctx *ctx)
{
	fmed_getmod_t getmod;
	ffdl dl = NULL;
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
	if (NULL != (minfo = core_findmod(&s)))
		goto iface;

	if (s.ptr[0] == '#') {
		if (ffstr_eqcz(&s, "#core"))
			getmod = &fmed_getmod_core;
		else if (ffstr_eqcz(&s, "#file"))
			getmod = &fmed_getmod_file;
		else if (ffstr_eqcz(&s, "#soundmod"))
			getmod = &fmed_getmod_sndmod;
		else if (ffstr_eqcz(&s, "#tui"))
			getmod = &fmed_getmod_tui;
		else if (ffstr_eqcz(&s, "#queue"))
			getmod = &fmed_getmod_queue;
		else {
			fferr_set(EINVAL);
			goto fail;
		}

	} else {

		char fn[FF_MAXFN];
		ffs_fmt(fn, fn + sizeof(fn), "%S.%s%Z", &s, FFDL_EXT);

		dbglog(core, NULL, "core", "loading module %s", fn);

		dl = ffdl_open(fn, 0);
		if (dl == NULL) {
			errlog(core, NULL, "core", "%e: %s: %s", FFERR_DLOPEN, ffdl_errstr(), fn);
			goto fail;
		}

		getmod = (void*)ffdl_addr(dl, "fmed_getmod");
		if (getmod == NULL) {
			errlog(core, NULL, "core", "%e: %s: %s", FFERR_DLADDR, ffdl_errstr(), "fmed_getmod");
			goto fail;
		}
	}

	if (NULL == ffarr_growT(&fmed->bmods, 1, 4, core_modinfo))
		goto fail;
	minfo = ffarr_endT(&fmed->bmods, core_modinfo);
	minfo->name = ffsz_alcopy(s.ptr, s.len);
	minfo->dl = dl;
	minfo->m = getmod(core);
	if (minfo->name == NULL || minfo->m == NULL)
		goto fail;

	if (0 != minfo->m->sig(FMED_SIG_INIT))
		goto fail;
	fmed->bmods.len++;

iface:
	mod->dl = dl;
	mod->m = minfo->m;
	mod->f = minfo->m->iface(modname.ptr);
	if (mod->f == NULL) {
		errlog(core, NULL, "core", "can't initialize %s", sname);
		goto fail2;
	}

	ffsz_fcopy(mod->name_s, sname, sname_len);
	mod->name = mod->name_s;
	fflist_ins(&fmed->mods, &mod->sib);

	if (ctx != NULL)
		mod->f->conf(ctx);

	return (fmed_modinfo*)mod;

fail:
	if (dl != NULL)
		ffdl_close(dl);
fail2:
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
	ffstr smod, iface;
	ffs_split2by(name, ffsz_len(name), '.', &smod, &iface);

	core_modinfo *mod;
	mod = core_findmod(&smod);
	if (mod == NULL) {
		errlog(core, NULL, "core", "module not found: %s", name);
		return NULL;
	}
	if (iface.len == 0)
		return mod;

	return mod->m->iface(iface.ptr);
}

static const fmed_modinfo* core_getmodinfo(const ffstr *name)
{
	core_mod *mod;
	FFLIST_WALK(&fmed->mods, mod, sib) {
		if (ffstr_eqz(name, mod->name))
			return (fmed_modinfo*)mod;
	}

	errlog(core, NULL, "core", "module not found: %S", name);
	return NULL;
}

const fmed_modinfo* core_modbyext(const ffstr3 *map, const ffstr *ext)
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

static int core_open(void)
{
	if (FF_BADFD == (fmed->kq = ffkqu_create())) {
		syserrlog(core, NULL, "core", "%e", FFERR_KQUCREAT);
		return 1;
	}
	core->kq = fmed->kq;
	ffkqu_post_attach(fmed->kq);

	fmed->pkqutime = ffkqu_settm(&fmed->kqutime, (uint)-1);
	ffkev_init(&fmed->evposted);
	fmed->evposted.oneshot = 0;
	fmed->evposted.handler = &core_posted;
	return 0;
}

void core_work(void)
{
	ffkqu_entry *ents = ffmem_tcalloc(ffkqu_entry, FMED_KQ_EVS);
	if (ents == NULL)
		return;

	while (!fmed->stopped) {

		int i;
		int nevents = ffkqu_wait(fmed->kq, ents, FMED_KQ_EVS, fmed->pkqutime);

		for (i = 0;  i < nevents;  i++) {
			ffkqu_entry *ev = &ents[i];
			ffkev_call(ev);

			fftask_run(&fmed->taskmgr);
		}

		if (nevents == -1 && fferr_last() != EINTR) {
			syserrlog(core, NULL, "core", "%e", FFERR_KQUWAIT);
			break;
		}
	}

	ffmem_free(ents);
}

static char* core_getpath(const char *name, size_t len)
{
	ffstr3 s = {0};
	if (0 == ffstr_catfmt(&s, "%S%*s%Z", &fmed->cmd.root, len, name)) {
		ffarr_free(&s);
		return NULL;
	}
	return s.ptr;
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

static int core_sig(uint signo)
{
	dbglog(core, NULL, "core", "received signal: %u", signo);

	switch (signo) {

	case FMED_CONF:
		core->loglev = fmed->cmd.debug ? FMED_LOG_DEBUG : FMED_LOG_INFO;
		if (0 != fmed_conf(0))
			return 1;
		if (0 != fmed_conf(1))
			return 1;
		break;

	case FMED_OPEN:
		if (0 != core_open())
			return 1;
		if (0 != core_sigmods(signo))
			return 1;
		break;

	case FMED_START:
		core_work();
		return 0;

	case FMED_STOP:
		core_sigmods(signo);
		fmed->stopped = 1;
		ffkqu_post(fmed->kq, &fmed->evposted, NULL);
		break;

	case FMED_LISTDEV:
		core_sigmods(signo);
		return 0;
	}

	return 0;
}


static void core_task(fftask *task, uint cmd)
{
	dbglog(core, NULL, "core", "task:%p, cmd:%u, active:%u, handler:%p, param:%p"
		, task, cmd, fftask_active(&fmed->taskmgr, task), task->handler, task->param);

	if (fftask_active(&fmed->taskmgr, task)) {
		if (cmd == FMED_TASK_DEL)
			fftask_del(&fmed->taskmgr, task);
		return;
	}

	if (cmd == FMED_TASK_POST) {
		if (1 == fftask_post(&fmed->taskmgr, task))
			ffkqu_post(fmed->kq, &fmed->evposted, NULL);
	}
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
	"error", "warning", "info", "debug",
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
