/** fmedia core.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/crc.h>
#include <FF/path.h>
#include <FFOS/error.h>
#include <FFOS/process.h>


typedef struct fmed_f {
	fflist_item sib;
	void *ctx;
	fmed_filt d;
	const fmed_modinfo *mod;
	unsigned opened :1;
} fmed_f;

typedef struct dict_ent {
	ffrbt_node nod;
	const char *name;
	union {
		int64 val;
		void *pval;
	};
} dict_ent;

struct fm_src {
	fflist_item sib;
	fftask task;
	struct {FFARR(fmed_f)} filters;
	fflist_cursor cur;
	ffrbtree dict;

	ffstr id;
	char sid[FFSLEN("*") + FFINT_MAXCHARS];

	unsigned err :1
		, capture :1;
};


fmedia *fmed;
fmed_core *core;

enum {
	FMED_KQ_EVS = 100
};

static fm_src* media_open1(int t, const char *fn);
static fmed_f* newfilter(fm_src *src, const char *modname);
static fmed_f* newfilter1(fm_src *src, const fmed_modinfo *mod);
static int media_open(fm_src *src, const char *fn);
static void media_open_capt(fm_src *src);
static void media_open_mix(fm_src *src);
static void media_free(fm_src *src);
static void media_process(void *udata);
static void media_onplay(void *udata);
static void media_onplay_hdlr(void *udata);
static fmed_f* media_modbyext(fm_src *src, const ffstr *ext);

static int64 trk_popval(void *trk, const char *name);
static int64 trk_getval(void *trk, const char *name);
static const char* trk_getvalstr(void *trk, const char *name);
static int trk_setval(void *trk, const char *name, int64 val);
static int trk_setvalstr(void *trk, const char *name, const char *val);
static const fmed_track _fmed_track = {
	&trk_popval, &trk_getval, &trk_getvalstr, &trk_setval, &trk_setvalstr
};

extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_mixer(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_tui(const fmed_core *_core);

static int core_open(void);
static int core_nextsrc(void);
static int core_opensrcs(void);
static int core_opensrcs_mix(void);
static int core_opendests(void);
static void core_playdone(void);

static void core_log(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...);
static char* core_getpath(const char *name, size_t len);
static int core_sig(uint signo);
static fmed_modinfo* core_getmod(const char *name);
static const fmed_modinfo* core_insmod(const char *name);
static void core_task(fftask *task, uint cmd);


static fmed_f* newfilter1(fm_src *src, const fmed_modinfo *mod)
{
	fmed_f *f = ffarr_push(&src->filters, fmed_f);
	FF_ASSERT(f != NULL);
	ffmem_tzero(f);
	f->d.track = &_fmed_track;
	f->d.handler = &media_process;
	f->d.trk = src;
	if (mod != NULL)
		f->mod = mod;
	return f;
}

static fmed_f* newfilter(fm_src *src, const char *modname)
{
	fmed_modinfo *mod;
	if (NULL == (mod = core_getmod(modname))) {
		errlog(core, NULL, "core", "module %s is not configured", modname);
		return NULL;
	}
	return newfilter1(src, mod);
}

static fmed_f* media_modbyext(fm_src *src, const ffstr *ext)
{
	const inmap_item *it = (void*)fmed->inmap.ptr;
	while (it != (void*)(fmed->inmap.ptr + fmed->inmap.len)) {
		size_t len = ffsz_len(it->ext);
		if (ffstr_eq(ext, it->ext, len)) {
			return newfilter1(src, it->mod);
		}
		it = (void*)((char*)it + sizeof(inmap_item) + len + 1);
	}

	errlog(core, NULL, "core", "unknown file format: %s", ext);
	return NULL;
}

static int media_open(fm_src *src, const char *fn)
{
	ffstr ext;

	if (fmed->rec_file.len != 0)
		trk_setval(src, "low_latency", 1);

	if (fmed->info)
		trk_setval(src, "input_info", 1);

	if (fmed->fseek)
		trk_setval(src, "input_seek", fmed->fseek);

	trk_setvalstr(src, "input", fn);
	newfilter(src, "#file.in");

	ffs_rsplit2by(fn, ffsz_len(fn), '.', NULL, &ext);
	if (NULL == media_modbyext(src, &ext))
		return 1;

	if (fmed->mix) {
		newfilter(src, "#mixer.in");
		return 0;
	}

	if (!fmed->silent)
		newfilter(src, "#tui.tui");

	if (fmed->outfn.len != 0) {
		trk_setvalstr(src, "output", fmed->outfn.ptr);
		newfilter(src, "wav.out");
		newfilter(src, "#file.out");

	} else if (fmed->output != NULL) {
		fmed_f *f = newfilter1(src, fmed->output);
		f->d.handler = &media_onplay_hdlr;
	}
	return 0;
}

static void media_open_capt(fm_src *src)
{
	fmed_f *f;

	trk_setval(src, "capture_device", fmed->captdev_name);

	trk_setval(src, "pcm_format", fmed->inp_pcm.format);
	trk_setval(src, "pcm_channels", fmed->inp_pcm.channels);
	trk_setval(src, "pcm_sample_rate", fmed->inp_pcm.sample_rate);
	f = newfilter1(src, fmed->input);
	f->d.handler = &media_onplay_hdlr;

	newfilter(src, "wav.out");

	trk_setvalstr(src, "output", fmed->rec_file.ptr);
	newfilter(src, "#file.out");
	src->capture = 1;
}

static void media_open_mix(fm_src *src)
{
	newfilter(src, "#mixer.out");

	if (fmed->outfn.len != 0) {
		trk_setvalstr(src, "output", fmed->outfn.ptr);
		newfilter(src, "wav.out");
		newfilter(src, "#file.out");

	} else if (fmed->output != NULL) {
		fmed_f *f = newfilter1(src, fmed->output);
		f->d.handler = &media_onplay_hdlr;
	}
}

enum T {
	M_IN
	, M_OUT
	, M_MIX
};

static fm_src* media_open1(int t, const char *fn)
{
	fmed_f *f, *prev = NULL;
	fm_src *src = ffmem_tcalloc1(fm_src);
	if (src == NULL)
		return NULL;
	ffrbt_init(&src->dict);

	src->id.len = ffs_fmt(src->sid, src->sid + sizeof(src->sid), "*%u", ++fmed->srcid);
	src->id.ptr = src->sid;

	trk_setval(src, "playdev_name", fmed->playdev_name);

	if (fmed->seek_time != 0)
		trk_setval(src, "seek_time", fmed->seek_time);

	if (fmed->until_time != 0) {
		if (fmed->until_time <= fmed->seek_time) {
			errlog(core, NULL, "core", "until_time must be bigger than seek_time");
			goto fail;
		}
		trk_setval(src, "until_time", fmed->until_time);
	}

	switch (t) {
	case M_OUT:
		if (NULL == ffarr_grow(&src->filters, 6, 0))
			goto fail;
		if (0 != media_open(src, fn))
			goto fail;
		break;

	case M_IN:
		if (NULL == ffarr_grow(&src->filters, 3, 0))
			goto fail;
		media_open_capt(src);
		break;

	case M_MIX:
		if (NULL == ffarr_grow(&src->filters, 3, 0))
			goto fail;
		media_open_mix(src);
		break;
	}

	FFARR_WALK(&src->filters, f) {

		if (f->mod == NULL)
			goto fail;

		if (prev != NULL)
			fflist_link(&f->sib, &prev->sib);
		else {
			f->d.flags = FMED_FLAST;
			f->ctx = f->mod->f->open(&f->d);
			if (f->ctx == NULL)
				goto fail;
			f->opened = 1;
			src->cur = &f->sib;
		}

		prev = f;
	}
	fflist_ins(&fmed->srcs, &src->sib);
	return src;

fail:
	media_free(src);
	return NULL;
}

static void media_free(fm_src *src)
{
	fmed_f *pf;

	dbglog(core, src, "core", "media: closing...");
	FFARR_WALK(&src->filters, pf) {
		if (pf->ctx != NULL)
			pf->mod->f->close(pf->ctx);
	}

	if (fftask_active(&fmed->taskmgr, &src->task))
		fftask_del(&fmed->taskmgr, &src->task);

	ffarr_free(&src->dict);
	if (fflist_exists(&fmed->srcs, &src->sib))
		fflist_rm(&fmed->srcs, &src->sib);
	dbglog(core, src, "core", "media: closed");
	ffmem_free(src);

	if (src == fmed->dst)
		fmed->dst = NULL;
}

static void media_onplay_hdlr(void *udata)
{
	fm_src *src = udata;
	if (fftask_active(&fmed->taskmgr, &src->task))
		return;
	fftask_post4(&fmed->taskmgr, &src->task, &media_onplay, src);
	ffkqu_post(fmed->kq, &fmed->evposted, NULL);
}

static void media_onplay(void *udata)
{
	fm_src *src = udata;
	media_process(src);
}

static void media_process(void *udata)
{
	fm_src *src = udata;
	fmed_f *nf;
	fmed_f *f;
	int r, e;

	for (;;) {
		if (src->err)
			goto fin;

		f = FF_GETPTR(fmed_f, sib, src->cur);
		if (f->d.datalen != 0 || (f->d.flags & FMED_FLAST)) {

			// dbglog(core, src, "core", "calling %s...", f->mod->name);
			e = f->mod->f->process(f->ctx, &f->d);
			// dbglog(core, src, "core", "%s returned: %u", f->mod->name, e);
			switch (e) {
			case FMED_RERR:
				goto fin;

			case FMED_RASYNC:
				return;

			case FMED_RMORE:
				r = FFLIST_CUR_PREV;
				break;

			case FMED_ROK:
				r = FFLIST_CUR_NEXT;
				break;
			case FMED_RDONE:
				f->d.datalen = 0;
				r = FFLIST_CUR_NEXT | FFLIST_CUR_RM;
				break;
			case FMED_RLASTOUT:
				f->d.datalen = 0;
				r = FFLIST_CUR_NEXT | FFLIST_CUR_RM | FFLIST_CUR_RMPREV;
				break;

			default:
				errlog(core, src, "core", "unknown return code from module: %u", e);
				goto fin;
			}

		} else
			r = FFLIST_CUR_PREV;

		if (f->d.datalen != 0)
			r |= FFLIST_CUR_SAMEIFBOUNCE;
		r = fflist_curshift(&src->cur, r | FFLIST_CUR_BOUNCE, fflist_sentl(&src->cur));

		switch (r) {
		case FFLIST_CUR_NONEXT:
			goto fin; //done

		case FFLIST_CUR_NOPREV:
			errlog(core, src, "core", "filter requires more input data");
			goto fin;

		case FFLIST_CUR_NEXT:
			nf = FF_GETPTR(fmed_f, sib, src->cur);
			nf->d.data = f->d.out;
			nf->d.datalen = f->d.outlen;
			if (src->cur->prev == fflist_sentl(&src->cur))
				nf->d.flags |= FMED_FLAST;

			if (!nf->opened) {
				dbglog(core, src, "core", "creating context for %s...", nf->mod->name);
				nf->ctx = nf->mod->f->open(&nf->d);
				if (nf->ctx == NULL)
					goto fin;
				dbglog(core, src, "core", "context for %s created", nf->mod->name);
				nf->opened = 1;
			}
			break;

		case FFLIST_CUR_SAME:
		case FFLIST_CUR_PREV:
			break;

		default:
			FF_ASSERT(0);
		}
	}

	return;

fin:
	media_free(src);
	if (fmed->mix) {
		if (fmed->srcs.len == 0)
			core_playdone();
	} else if (!src->capture) {
		core_nextsrc();
	} else {
		fmed->recording = 0;
		core_playdone();
	}
}


static dict_ent* dict_find(fm_src *src, const char *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_getz(name, 0);
	ffrbt_node *nod;

	nod = ffrbt_find(&src->dict, crc, NULL);
	if (nod == NULL)
		return NULL;
	ent = (dict_ent*)nod;

	if (0 != ffsz_cmp(name, ent->name))
		return NULL;

	return ent;
}

static dict_ent* dict_add(fm_src *src, const char *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_getz(name, 0);
	ffrbt_node *nod, *parent;

	nod = ffrbt_find(&src->dict, crc, &parent);
	if (nod != NULL) {
		ent = (dict_ent*)nod;
		if (0 != ffsz_cmp(name, ent->name)) {
			errlog(core, NULL, "core", "setval: CRC collision: %u, key: %s, with key: %s"
				, crc, name, ent->name);
			src->err = 1;
			return NULL;
		}

	} else {
		ent = ffmem_talloc(dict_ent, 1);
		if (ent == NULL) {
			errlog(core, NULL, "core", "setval: %e", FFERR_BUFALOC);
			src->err = 1;
			return NULL;
		}
		ent->nod.key = crc;
		ffrbt_insert(&src->dict, &ent->nod, parent);
		ent->name = name;
	}

	return ent;
}


static int64 trk_popval(void *trk, const char *name)
{
	fm_src *src = trk;
	dict_ent *ent = dict_find(src, name);
	if (ent != NULL) {
		int64 val = ent->val;
		ffrbt_rm(&src->dict, &ent->nod);
		return val;
	}

	return FMED_NULL;
}

static int64 trk_getval(void *trk, const char *name)
{
	fm_src *src = trk;
	dict_ent *ent = dict_find(src, name);
	if (ent != NULL)
		return ent->val;
	return FMED_NULL;
}

static const char* trk_getvalstr(void *trk, const char *name)
{
	fm_src *src = trk;
	dict_ent *ent = dict_find(src, name);
	if (ent != NULL)
		return ent->pval;
	return FMED_PNULL;
}

static int trk_setval(void *trk, const char *name, int64 val)
{
	fm_src *src = trk;
	dict_ent *ent = dict_add(src, name);
	if (ent == NULL)
		return 0;

	ent->val = val;
	dbglog(core, trk, "core", "setval: %s = %D", name, val);
	return 0;
}

static int trk_setvalstr(void *trk, const char *name, const char *val)
{
	fm_src *src = trk;
	dict_ent *ent = dict_add(src, name);
	if (ent == NULL)
		return 0;

	ent->pval = (void*)val;
	dbglog(core, trk, "core", "setval: %s = %s", name, val);
	return 0;
}


static void core_playdone(void)
{
	fmed->playing = 0;
	if (!fmed->recording) {
		fmed->stopped = 1;
		ffkqu_post(fmed->kq, &fmed->evposted, NULL);
	}
}

static int core_nextsrc(void)
{
	const char **pfn;
	fm_src *src;

	if (fmed->in_files_cur == fmed->in_files.len && fmed->repeat_all)
		fmed->in_files_cur = 0;

	pfn = ((const char **)fmed->in_files.ptr) + fmed->in_files_cur;
	for (;  fmed->in_files_cur < fmed->in_files.len;  pfn++) {

		fmed->in_files_cur++;

		src = media_open1(M_OUT, *pfn);
		if (src == NULL)
			continue;

		media_process(src);
		return 0;
	}

	core_playdone();
	return 0;
}

static int core_opensrcs_mix(void)
{
	const char **fn = (void*)fmed->in_files.ptr;
	fm_src *src, *mout;
	int i;

	//mixer-out MUST be initialized before any mixer-in instances
	mout = media_open1(M_MIX, NULL);

	for (i = 0;  i < fmed->in_files.len;  i++, fn++) {

		src = media_open1(M_OUT, *fn);
		if (src == NULL)
			continue;

		media_process(src);
	}

	media_process(mout);
	return 0;
}

static int core_opensrcs(void)
{
	return core_nextsrc();
}

static int core_opendests(void)
{
	if (fmed->rec_file.len != 0) {
		fmed->dst = media_open1(M_IN, NULL);
		if (fmed->dst == NULL)
			return 1;

		media_process(fmed->dst);
	}

	return 0;
}

static void core_posted(void *udata)
{
}


int core_init(void)
{
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return 1;

	fmed->kq = FF_BADFD;
	fftask_init(&fmed->taskmgr);
	fflist_init(&fmed->srcs);
	fmed->core.log = &core_log;
	fmed->core.getpath = &core_getpath;
	fmed->core.sig = &core_sig;
	fmed->core.getmod = &core_getmod;
	fmed->core.insmod = &core_insmod;
	fmed->core.task = &core_task;
	core = &fmed->core;
	return 0;
}

void core_free(void)
{
	uint i;
	fm_src *src;
	fflist_item *next;
	char **fn;
	fmed_modinfo *mod;

	FFLIST_WALKSAFE(&fmed->srcs, src, sib, next) {
		media_free(src);
	}

	if (fmed->dst != NULL)
		media_free(fmed->dst);

	fn = (char**)fmed->in_files.ptr;
	for (i = 0;  i < fmed->in_files.len;  i++, fn++) {
		ffmem_free(*fn);
	}
	ffarr_free(&fmed->in_files);
	ffstr_free(&fmed->outfn);
	ffstr_free(&fmed->rec_file);

	ffarr_free(&fmed->inmap);

	FFARR_WALK(&fmed->mods, mod) {
		mod->m->destroy();
		if (mod->dl != NULL)
			ffdl_close(mod->dl);
		ffmem_free(mod->name);
	}

	if (fmed->kq != FF_BADFD)
		ffkqu_close(fmed->kq);

	ffstr_free(&fmed->root);
	ffmem_free(fmed);
	fmed = NULL;
}

static const fmed_modinfo* core_insmod(const char *sname)
{
	const char *modname, *dot;
	fmed_getmod_t getmod;
	ffdl dl = NULL;
	ffstr s;
	fmed_modinfo *mod = ffarr_push(&fmed->mods, fmed_modinfo);
	if (mod == NULL)
		return NULL;
	fmed->mods.len--;

	ffstr_setz(&s, sname);
	dot = ffs_findc(s.ptr, s.len, '.');
	if (dot == NULL || dot == s.ptr || dot + 1 == s.ptr + s.len) {
		fferr_set(EINVAL);
		return NULL;
	}
	modname = dot + 1;
	s.len = dot - s.ptr;

	if (s.ptr[0] == '#') {
		ffstr_shift(&s, 1);

		if (ffstr_eqcz(&s, "file"))
			getmod = &fmed_getmod_file;
		else if (ffstr_eqcz(&s, "mixer"))
			getmod = &fmed_getmod_mixer;
		else if (ffstr_eqcz(&s, "tui"))
			getmod = &fmed_getmod_tui;
		else {
			fferr_set(EINVAL);
			return NULL;
		}

	} else {

		char fn[FF_MAXFN];
		ffs_fmt(fn, fn + sizeof(fn), "%S.%s%Z", &s, FFDL_EXT);
		dl = ffdl_open(fn, 0);
		if (dl == NULL)
			return NULL;

		getmod = (void*)ffdl_addr(dl, "fmed_getmod");
		if (getmod == NULL)
			goto fail;
	}

	mod->m = getmod(core);
	if (mod->m == NULL)
		goto fail;

	mod->f = mod->m->iface(modname);
	if (mod->f == NULL)
		goto fail;

	mod->dl = dl;
	mod->name = ffsz_alcopyz(sname);
	if (mod->name == NULL)
		goto fail;
	fmed->mods.len++;
	return mod;

fail:
	if (dl != NULL)
		ffdl_close(dl);
	return NULL;
}

static fmed_modinfo* core_getmod(const char *name)
{
	fmed_modinfo *mod;
	FFARR_WALK(&fmed->mods, mod) {
		if (!ffsz_cmp(mod->name, name))
			return mod;
	}

	return NULL;
}

static int core_open(void)
{
	if (FF_BADFD == (fmed->kq = ffkqu_create())) {
		syserrlog(core, NULL, "core", "%e", FFERR_KQUCREAT);
		return 1;
	}
	fmed->core.loglev = fmed->debug ? FMED_LOG_DEBUG : 0;
	fmed->core.kq = fmed->kq;

	fmed->pkqutime = ffkqu_settm(&fmed->kqutime, (uint)-1);

	if (fmed->srcs.len != 0) {
		fmed->playing = 1;
	}

	if (fmed->rec_file.len != 0) {
		fmed->recording = 1;
	}

	if (!fmed->mix && 0 != core_opensrcs())
		return 1;
	else if (fmed->mix && 0 != core_opensrcs_mix())
		return 1;

	if (0 != core_opendests())
		return 1;

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
	if (0 == ffstr_catfmt(&s, "%S%*s%Z", &fmed->root, len, name)) {
		ffarr_free(&s);
		return NULL;
	}
	return s.ptr;
}

static int core_sig(uint signo)
{
	fmed_modinfo *mod;

	switch (signo) {

	case FMED_OPEN:
		return core_open();

	case FMED_START:
		core_work();
		return 0;

	case FMED_STOP:
	case FMED_LISTDEV:
		FFARR_WALK(&fmed->mods, mod) {
			if (0 != mod->m->sig(signo))
				break;
		}
		return 0;
	}

	return 0;
}


static void core_task(fftask *task, uint cmd)
{
	if (fftask_active(&fmed->taskmgr, task)) {
		if (cmd == FMED_TASK_DEL)
			fftask_del(&fmed->taskmgr, task);
		return;
	}

	if (cmd == FMED_TASK_POST) {
		fftask_post(&fmed->taskmgr, task);
	}
}

static void core_log(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...)
{
	fm_src *src = trk;
	va_list va;
	va_start(va, fmt);
	fmed_log(fd, NULL, module, level, (src != NULL) ? &src->id : NULL, fmt, va);
	va_end(va);
}
