/** fmedia core.
Copyright (c) 2015 Simon Zolin */

#include <core/core-priv.h>
#include <util/path.h>
#include <util/taskqueue.h>
#ifdef FF_WIN
#include <util/wohandler.h>
#endif
#include <FFOS/error.h>
#include <FFOS/timer.h>
#include <FFOS/dir.h>
#include <FFOS/thread.h>
#include <FFOS/file.h>

#define PP_STR(ppdef)  _PP_STR(ppdef)
#define _PP_STR(val)  #val

#ifndef FMED_VER_SUF
	#define FMED_VER_SUF  "*"
#endif

typedef struct fmedia {
	ffvec workers; //worker[]
	ffkqu_time kqutime;

	uint stopped;

	ffvec bmods; //core_modinfo[]
	fflist mods; //core_mod[]

	ffenv env;
	ffstr root;
	fmed_config conf;
	fmed_props props;
	fftime_zone tz;

	const fmed_queue *qu;
	const fmed_log *log;

#ifdef FF_WIN
	ffwoh *woh;
#endif
} fmedia;

struct worker {
	ffthd thd;
	ffthd_id id;
	fffd kq;

	fftaskmgr taskmgr;
	ffkevpost kqpost;
	ffkevent evposted;

	fftimer timer;
	fftimerqueue timerq;
	uint timer_period;
	ffkevent timer_kev;

	ffatomic njobs;
	uint init :1;
};

typedef struct core_modinfo {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	void *iface; //dummy

	fflock lock;
	uint opened :1;
} core_modinfo;

static fmedia *fmed;

enum {
	FMED_KQ_EVS = 8,
	TMR_INT = 250,
};


FF_EXP fmed_core* core_init(char **argv, char **env);
FF_EXP void core_free(void);

extern int tracks_init(void);
extern void tracks_destroy(void);

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_globcmd(const fmed_core *_core);
#ifdef FF_WIN
extern const fmed_mod* fmed_getmod_winsleep(const fmed_core *_core);
#endif

static int core_open(void);
static int core_filetype(const char *fn);

#include <core/core-mod.h>
#include <core/core-work.h>

static fmed_core _fmed_core;
fmed_core *core;

//LOG
static void log_dummy_func(uint flags, fmed_logdata *ld)
{
}
static const fmed_log log_dummy = {
	&log_dummy_func
};

static int core_conf(const char *filename)
{
	int r = -1;
	char *fn = NULL;

	if (NULL == (fmed->props.user_path = ffenv_expand(&fmed->env, NULL, 0, FFDIR_USER_CONFIG "/fmedia/")))
		goto end;

	if (filename != NULL)
		fn = (void*)filename;
	else if (NULL == (fn = core->getpath(FFSTR(FMED_GLOBCONF))))
		goto end;

	fftime t0, t1;
	if (core->loglev == FMED_LOG_DEBUG)
		t0 = fftime_monotonic();

	if (0 != core_conf_parse(&fmed->conf, fn, 0))
		goto end;

	if (core->loglev == FMED_LOG_DEBUG) {
		t1 = fftime_monotonic();
		fftime_sub(&t1, &t0);
		dbglog0("conf process time: %uus", t1.sec*1000000 + t1.nsec/1000);
	}

	fmed->props.record_format = fmed->conf.inp_pcm;
	fmed->props.prevent_sleep = fmed->conf.prevent_sleep;
	fmed->props.codepage = fmed->conf.codepage;

	r = 0;
end:
	if (fn != filename)
		ffmem_free(fn);
	return r;
}


fmed_core* core_init(char **argv, char **env)
{
	core = &_fmed_core;
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return NULL;
	fmed->log = &log_dummy;
	if (0 != ffenv_init(&fmed->env, env))
		goto err;

	fftime_local(&fmed->tz);

	fflist_init(&fmed->mods);
	core_insmodz("#core.core", NULL);
	core_insmodz("#core.track", NULL);
	core_insmodz("#queue.queue", NULL);

	ffkqu_settm(&fmed->kqutime, (uint)-1);

	if (0 != conf_init(&fmed->conf)) {
		goto err;
	}

	char fn[FF_MAXPATH];
	ffstr path;
	const char *p;
	if (NULL == (p = ffps_filename(fn, sizeof(fn), argv[0])))
		goto err;
	if (NULL == ffpath_split2(p, ffsz_len(p), &path, NULL))
		goto err;
	if (NULL == ffstr_dup(&fmed->root, path.ptr, path.len + FFSLEN("/")))
		goto err;

	core->loglev = FMED_LOG_INFO;

	fmed->props.version_str = PP_STR(FMED_VER_MAJOR) "." PP_STR(FMED_VER_MINOR) FMED_VER_SUF;
#if FMED_VER_PATCH != 0
	fmed->props.version_str = PP_STR(FMED_VER_MAJOR) "." PP_STR(FMED_VER_MINOR) "." PP_STR(FMED_VER_PATCH) FMED_VER_SUF;
#endif

	ffenv_locale(fmed->props.language, sizeof(fmed->props.language), FFENV_LANGUAGE);
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
	FFSLICE_WALK(&fmed->workers, w) {
		if (w->init)
			wrk_destroy(w);
	}
	tracks_destroy();
	ffvec_free(&fmed->workers);

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		mod_freeiface(mod);
	}

	core_modinfo *minfo;
	FFSLICE_WALK(&fmed->bmods, minfo) {
		somod_destroy(minfo);
	}
	ffvec_free(&fmed->bmods);

	conf_destroy(&fmed->conf);
	ffmem_free(fmed->props.user_path);
	ffstr_free(&fmed->root);
	ffenv_destroy(&fmed->env);

#ifdef FF_WIN
	FF_SAFECLOSE(fmed->woh, NULL, ffwoh_free);
#endif

	ffmem_free0(fmed);
}

static int core_open(void)
{
	uint n = fmed->conf.workers;
	if (n == 0) {
		ffsysconf sc;
		ffsc_init(&sc);
		n = ffsc_get(&sc, FFSYSCONF_NPROCESSORS_ONLN);
	}
	if (NULL == ffvec_zallocT(&fmed->workers, n, struct worker))
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

static char* core_getpath(const char *name, size_t len)
{
	return ffsz_allocfmt("%S%*s", &fmed->root, len, name);
}

static char* core_env_expand(char *dst, size_t cap, const char *src)
{
	return ffenv_expand(&fmed->env, dst, cap, src);
}

static int core_filetype_ext(const ffstr *ext)
{
	if (NULL == core_getmod2(FMED_MOD_INEXT, ext->ptr, ext->len))
		return FMED_FT_UKN;

	if (ffstr_eqcz(ext, "m3u8")
		|| ffstr_eqcz(ext, "m3u")
		|| ffstr_eqcz(ext, "m3uz")
		|| ffstr_eqcz(ext, "pls")
		|| ffstr_eqcz(ext, "cue"))
		return FMED_FT_PLIST;

	return FMED_FT_FILE;
}

static int core_filetype(const char *fn)
{
	ffstr ext;
	fffileinfo fi;

	if (0 == fffile_infofn(fn, &fi))
		if (fffile_isdir(fffile_infoattr(&fi)))
			return FMED_FT_DIR;

	ffpath_split3(fn, ffsz_len(fn), NULL, NULL, &ext);
	return core_filetype_ext(&ext);
}

static const char* ifilter_byext(const char *ext)
{
	const fmed_modinfo *mod;
	ffstr s = FFSTR_INITZ(ext);
	if (NULL == (mod = modbyext(&fmed->conf.in_ext_map, &s)))
		goto err;
	if (mod->m == NULL && 0 != mod_load_delayed((core_mod*)mod, 0))
		goto err;
	return mod->name;
err:
	errlog0("module not found: %s", ext);
	return NULL;
}

static const char* const sig_str[] = {
	"FMED_SIG_INIT",
	"FMED_CONF",
	"FMED_OPEN",
	"FMED_START",
	"FMED_STOP",
	"FMED_SIG_INSTALL",
	"FMED_SIG_UNINSTALL",
	"FMED_FILETYPE",
	"FMED_WOH_INIT",
	"FMED_WOH_ADD",
	"FMED_WOH_DEL",
	"FMED_TASK_XPOST",
	"FMED_TASK_XDEL",
	"FMED_WORKER_ASSIGN",
	"FMED_WORKER_RELEASE",
	"FMED_WORKER_AVAIL",
	"FMED_SETLOG",
	"FMED_TZOFFSET",
	"FMED_FILETYPE_EXT",
	"FMED_IFILTER_BYEXT",
	"FMED_OFILTER_BYEXT",
	"FMED_FILTER_BYNAME",
};

static ssize_t core_cmd(uint signo, ...)
{
	ssize_t r = 0;
	va_list va;
	va_start(va, signo);

	FF_ASSERT(signo < FF_COUNT(sig_str));
	dbglog0("received signal: %s"
		, sig_str[signo]);

	switch (signo) {

	case FMED_CONF: {
		const char *fn = va_arg(va, char*);
		if (0 != core_conf(fn)) {
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
		work_loop(fmed->workers.ptr);
		break;

	case FMED_STOP: {
		FF_ASSERT(core_ismainthr());
		if (fmed->stopped)
			break;
		core_sigmods(signo);
		FF_WRITEONCE(fmed->stopped, 1);
		struct worker *w;
		FFSLICE_WALK(&fmed->workers, w) {
			if (w->init) {
				if (0 != ffkqu_post(&w->kqpost, &w->evposted))
					syserrlog("%s", "ffkqu_post");
			}
		}
		break;
	}

	case FMED_FILETYPE:
		r = core_filetype(va_arg(va, char*));
		break;
	case FMED_FILETYPE_EXT:
		r = core_filetype_ext(va_arg(va, ffstr*));
		break;

	case FMED_TASK_XPOST:
	case FMED_TASK_XDEL: {
		fftask *task = va_arg(va, fftask*);
		uint wid = va_arg(va, uint);
		r = xtask(signo, task, wid);
		break;
	}

	case FMED_WORKER_ASSIGN: {
		fffd *pkq = va_arg(va, fffd*);
		uint flags = va_arg(va, uint);
		r = work_assign(flags);
		struct worker *w = ffslice_itemT(&fmed->workers, r, struct worker);
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

	case FMED_SETLOG: {
		void *logger = va_arg(va, void*);
		fmed->log = logger;
		break;
	}

	case FMED_TZOFFSET:
		r = fmed->tz.real_offset;
		break;

	case FMED_IFILTER_BYEXT:
		r = (ffsize)ifilter_byext(va_arg(va, char*));
		break;

	case FMED_FILTER_BYNAME: {
		const char *name = va_arg(va, char*);
		r = (size_t)core->getmod(name);
		break;
	}
	}

	va_end(va);
	return r;
}

static int core_sig(uint signo)
{
	return core_cmd(signo);
}

static int64 core_getval(const char *name)
{
	if (!ffsz_cmp(name, "codepage"))
		return fmed->conf.codepage;
	else if (!ffsz_cmp(name, "instance_mode"))
		return fmed->conf.instance_mode;
	return FMED_NULL;
}

static const char *const loglevs[] = {
	"error", "warning", "info", "info", "debug",
};

static void core_logv(uint flags, void *trk, const char *module, const char *fmt, va_list va)
{
	char stime[32];
	ffdatetime dt;
	fftime t;
	size_t r;
	fmed_logdata ld = {};
	uint lev = flags & _FMED_LOG_LEVMASK;
	int e;

	if (!(core->loglev >= (flags & _FMED_LOG_LEVMASK)))
		return;

	if (flags & FMED_LOG_SYS)
		e = fferr_last();

	fftime_now(&t);
	t.sec += FFTIME_1970_SECONDS + fmed->tz.real_offset;
	fftime_split1(&dt, &t);
	r = fftime_tostr1(&dt, stime, sizeof(stime), FFTIME_HMS_MSEC);
	stime[r] = '\0';
	ld.stime = stime;
	ld.tid = ffthd_curid();

	FF_ASSERT(lev != 0);
	FF_ASSERT(FF_COUNT(loglevs) == FMED_LOG_DEBUG);
	ld.level = loglevs[lev - 1];

	ld.ctx = NULL;
	ld.module = module;
	if (trk != NULL) {
		_fmed_track.loginfo(trk, &ld.ctx, &module);
		ld.trk = trk;
		if (ld.module == NULL)
			ld.module = module;
	}

	if (flags & FMED_LOG_SYS)
		fferr_set(e);

	ld.fmt = fmt;
	va_copy(ld.va, va);
	fmed->log->log(flags, &ld);
	va_end(ld.va);
}

static void core_log(uint flags, void *trk, const char *module, const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	core_logv(flags, trk, module, fmt, va);
	va_end(va);
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

static const fmed_mod fmed_core_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&core_iface, &core_sig2, &core_destroy
};

static const fmed_mod* fmed_getmod_core(const fmed_core *_core)
{
	return &fmed_core_mod;
}


static fmed_core _fmed_core = {
	0, NULL, 0,
	core_getval,
	core_log, core_logv,
	core_getpath, core_env_expand,
	core_sig, core_cmd,
	core_getmod, core_getmod2, core_insmodz,
	core_task,
	core_timer,
};
