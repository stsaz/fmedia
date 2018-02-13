/** fmedia track.
Copyright (c) 2016 Simon Zolin */

#include <core.h>

#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/crc.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FF/sys/filemap.h>
#include <FFOS/error.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>


#undef dbglog
#undef errlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "track", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "track", __VA_ARGS__)


enum {
	N_RUNTIME_FILTERS = 4, //allow up to this number of filters to be added while track is running
};

typedef struct fmed_f {
	fflist_item sib;
	void *ctx;
	struct {
		size_t datalen;
		const char *data;
	} d;
	const char *name;
	const fmed_filter *filt;
	fftime clk;
	unsigned opened :1
		, newdata :1
		, want_input :1;
} fmed_f;

typedef struct dict_ent {
	ffrbt_node nod;
	const char *name;
	union {
		int64 val;
		void *pval;
	};
	uint acq :1;
} dict_ent;

enum TRK_ST {
	TRK_ST_STOPPED,
	TRK_ST_ACTIVE,
	TRK_ST_PAUSED,
	TRK_ST_ERR,
};

typedef struct fm_trk {
	fflist_item sib;
	fmed_trk props;
	ffchain filt_chain;
	struct {FFARR(fmed_f)} filters;
	fflist_cursor cur;
	ffrbtree dict;
	ffrbtree meta;
	struct ffps_perf psperf;
	fftask tsk;

	ffstr id;
	char sid[FFSLEN("*") + FFINT_MAXCHARS];

	uint state; //enum TRK_ST
} fm_trk;


static int trk_setout(fm_trk *t);
static int trk_opened(fm_trk *t);
static fmed_f* addfilter(fm_trk *t, const char *modname);
static fmed_f* addfilter1(fm_trk *t, const fmed_modinfo *mod);
static int filt_call(fm_trk *t, fmed_f *f);
static int trk_open(fm_trk *t, const char *fn);
static void trk_open_capt(fm_trk *t);
static void trk_free(fm_trk *t);
static void trk_process(void *udata);
static void trk_stop(fm_trk *t, uint flags);
static fmed_f* trk_modbyext(fm_trk *t, uint flags, const ffstr *ext);
static void trk_printtime(fm_trk *t);
static int trk_meta_enum(fm_trk *t, fmed_trk_meta *meta);
static int trk_meta_copy(fm_trk *t, fm_trk *src);
static fmed_f* filt_add(fm_trk *t, uint cmd, const char *name);

static dict_ent* dict_add(fm_trk *t, const char *name, uint *f);
static void dict_ent_free(dict_ent *e);

// TRACK
static void* trk_create(uint cmd, const char *url);
static fmed_trk* trk_conf(void *trk);
static void trk_copy_info(fmed_trk *dst, const fmed_trk *src);
static ssize_t trk_cmd(void *trk, uint cmd, ...);
static int trk_cmd2(void *trk, uint cmd, void *param);
static void trk_loginfo(void *trk, const ffstr **id, const char **module);
static int64 trk_popval(void *trk, const char *name);
static int64 trk_getval(void *trk, const char *name);
static const char* trk_getvalstr(void *trk, const char *name);
static int trk_setval(void *trk, const char *name, int64 val);
static int trk_setvalstr(void *trk, const char *name, const char *val);
static int64 trk_setval4(void *trk, const char *name, int64 val, uint flags);
static char* trk_setvalstr4(void *trk, const char *name, const char *val, uint flags);
static char* trk_getvalstr3(void *trk, const void *name, uint flags);
const fmed_track _fmed_track = {
	&trk_create, &trk_conf, &trk_copy_info, &trk_cmd, &trk_cmd2,
	&trk_popval, &trk_getval, &trk_getvalstr, &trk_setval, &trk_setvalstr, &trk_setval4, &trk_setvalstr4, &trk_getvalstr3,
	&trk_loginfo,
};


static fmed_f* _addfilter1(fm_trk *t)
{
	fmed_f *f;
	if (NULL == (f = ffarr_pushgrowT((ffarr*)&t->filters, 4, fmed_f)))
		return NULL;
	ffmem_tzero(f);
	return f;
}

static fmed_f* addfilter1(fm_trk *t, const fmed_modinfo *mod)
{
	fmed_f *f = _addfilter1(t);
	f->name = mod->name;
	f->filt = mod->iface;
	return f;
}

static fmed_f* addfilter(fm_trk *t, const char *modname)
{
	fmed_f *f = _addfilter1(t);
	f->name = modname;
	f->filt = core->getmod(modname);
	return f;
}

static fmed_f* trk_modbyext(fm_trk *t, uint flags, const ffstr *ext)
{
	const fmed_modinfo *mi = core->getmod2(flags, ext->ptr, ext->len);
	if (mi == NULL)
		return NULL;
	return addfilter1(t, mi);
}

static int trk_open(fm_trk *t, const char *fn)
{
	ffstr name, ext;
	fffileinfo fi;

	trk_setvalstr(t, "input", fn);
	addfilter(t, "#queue.track");

	if (0 == fffile_infofn(fn, &fi) && fffile_isdir(fffile_infoattr(&fi))) {
		addfilter(t, "plist.dir");
		return 0;
	}

	if (ffs_match(fn, ffsz_len(fn), "http://", 7)) {
		addfilter(t, "net.icy");
	} else {
		uint have_path = (NULL != ffpath_split2(fn, ffsz_len(fn), NULL, &name));
		ffpath_splitname(name.ptr, name.len, &name, &ext);
		if (!have_path && ffstr_eqcz(&name, "@stdin"))
			addfilter(t, "#file.stdin");
		else
			addfilter(t, "#file.in");

		if (NULL == trk_modbyext(t, FMED_MOD_INEXT, &ext))
			return 1;
	}

	return 0;
}

static void trk_open_capt(fm_trk *t)
{
	ffpcm_fmtcopy(&t->props.audio.fmt, &fmed->conf.inp_pcm);

	if (fmed->conf.inp_pcm.channels & ~FFPCM_CHMASK) {
		t->props.audio.convfmt.channels = fmed->conf.inp_pcm.channels;
		t->props.audio.fmt.channels = fmed->conf.inp_pcm.channels & FFPCM_CHMASK;
	}

	addfilter1(t, fmed->conf.input);

	addfilter(t, "#soundmod.until");
	addfilter(t, "#soundmod.rtpeak");
}

static int trk_setout(fm_trk *t)
{
	ffstr name, ext;
	const char *s;
	ffbool stream_copy = t->props.stream_copy;

	if (t->props.type == FMED_TRK_TYPE_NETIN) {
		ffstr ext;
		const char *input = trk_getvalstr(t, "input");
		ffpath_splitname(input, ffsz_len(input), NULL, &ext);
		if (NULL == trk_modbyext(t, FMED_MOD_INEXT, &ext))
			return -1;

	} else if (t->props.type == FMED_TRK_TYPE_NONE) {
		return 0;

	} else if (t->props.type != FMED_TRK_TYPE_MIXIN) {
		if (t->props.type != FMED_TRK_TYPE_REC)
			addfilter(t, "#soundmod.until");
		if (fmed->cmd.gui)
			addfilter(t, "gui.gui");
		else if (!fmed->cmd.notui)
			addfilter(t, "tui.tui");
	}

	if (t->props.type != FMED_TRK_TYPE_MIXOUT && !stream_copy) {
		addfilter(t, "#soundmod.gain");
	}

	addfilter(t, "#soundmod.conv");

	if (t->props.type == FMED_TRK_TYPE_MIXIN) {
		addfilter(t, "mixer.in");

	} else if (t->props.pcm_peaks) {
		addfilter(t, "#soundmod.peaks");

	} else if (FMED_PNULL != (s = trk_getvalstr(t, "output"))) {
		uint have_path = (NULL != ffpath_split2(s, ffsz_len(s), NULL, &name));
		ffs_rsplit2by(name.ptr, name.len, '.', &name, &ext);

		if (NULL == trk_modbyext(t, FMED_MOD_OUTEXT, &ext))
			return -1;

		if (!have_path && ffstr_eqcz(&name, "@stdout")) {
			addfilter(t, "#file.stdout");
			t->props.out_seekable = 0;
		} else {
			addfilter(t, "#file.out");
			t->props.out_seekable = 1;
		}

	} else if (fmed->conf.output != NULL) {
		addfilter1(t, fmed->conf.output);
	}

	return 0;
}

static int trk_opened(fm_trk *t)
{
	fmed_f *f;

	if (NULL == ffarr_grow(&t->filters, N_RUNTIME_FILTERS, 0))
		return -1;

	FFARR_WALK(&t->filters, f) {
		ffchain_add(&t->filt_chain, &f->sib);
	}

	if (core->loglev == FMED_LOG_DEBUG) {
		ffarr schain = {0};
		FFARR_WALK(&t->filters, f) {
			const char *next = (f == t->filters.ptr) ? "" : " -> ";
			ffstr_catfmt(&schain, "%s%s", next, f->name);
		}
		dbglog(t, "chain: %S", &schain);
		ffarr_free(&schain);
		dbglog(t, "properties: %*xb", sizeof(t->props), &t->props);
	}

	fflist_ins(&fmed->trks, &t->sib);
	t->state = TRK_ST_ACTIVE;
	t->cur = &t->filters.ptr->sib;
	return 0;
}

/*
Example of a typical chain:
 #queue.track
 -> INPUT
 -> DECODER -> (#soundmod.until) -> UI -> #soundmod.gain -> (#soundmod.conv/conv-soxr) -> (ENCODER)
 -> OUTPUT
*/
static void* trk_create(uint cmd, const char *fn)
{
	fm_trk *t = ffmem_tcalloc1(fm_trk);
	if (t == NULL)
		return NULL;
	ffchain_init(&t->filt_chain);
	t->cur = ffchain_sentl(&t->filt_chain);
	ffrbt_init(&t->dict);
	ffrbt_init(&t->meta);
	fftask_set(&t->tsk, &trk_process, t);

	trk_copy_info(&t->props, NULL);
	t->props.track = &_fmed_track;
	t->props.handler = &trk_process;
	t->props.trk = t;

	t->id.len = ffs_fmt(t->sid, t->sid + sizeof(t->sid), "*%u", ++fmed->trkid);
	t->id.ptr = t->sid;

	if (NULL == ffarr_allocT((ffarr*)&t->filters, 8, fmed_f))
		goto err;

	switch (cmd) {

	case FMED_TRK_TYPE_PLAYBACK:
		if (0 != trk_open(t, fn)) {
			trk_free(t);
			return FMED_TRK_EFMT;
		}
		t->props.type = FMED_TRK_TYPE_PLAYBACK;
		break;

	case FMED_TRK_TYPE_REC:
		trk_open_capt(t);
		t->props.type = FMED_TRK_TYPE_REC;
		break;

	case FMED_TRK_TYPE_MIXOUT:
		addfilter(t, "#queue.track");
		addfilter(t, "mixer.out");
		t->props.type = FMED_TRK_TYPE_MIXOUT;
		break;

	default:
		if (cmd >= _FMED_TRK_TYPE_END) {
			errlog(t, "unknown track type:%u", cmd);
			goto err;
		}
		t->props.type = cmd;
		break;
	}

	return t;

err:
	trk_free(t);
	return NULL;
}

static fmed_trk* trk_conf(void *trk)
{
	fm_trk *t = trk;
	return &t->props;
}

static void trk_copy_info(fmed_trk *dst, const fmed_trk *src)
{
	if (src == NULL) {
		ffmem_tzero(dst);
		dst->datatype = "";
		dst->audio.total = FMED_NULL;
		dst->audio.seek = FMED_NULL;
		dst->audio.until = FMED_NULL;
		memset(&dst->aac, 0xff, FFOFF(fmed_trk, bits) - FFOFF(fmed_trk, aac));
		dst->audio.decoder = "";
		ffstr_null(&dst->aac.profile);
		fftime_null(&dst->mtime);
		return;
	}
	ffmemcpy(&dst->audio, &src->audio, FFOFF(fmed_trk, bits) - FFOFF(fmed_trk, audio));
	dst->bits = src->bits;
}

static void trk_stop(fm_trk *t, uint flags)
{
	trk_setval(t, "stopped", flags);
	t->props.flags |= FMED_FSTOP;
	if (t->state != TRK_ST_ACTIVE)
		trk_free(t);
}

static void trk_printtime(fm_trk *t)
{
	fmed_f *pf;
	ffstr3 s = {0};
	fftime all = {0};

	FFARR_WALK(&t->filters, pf) {
		fftime_add(&all, &pf->clk);
	}
	if (fftime_empty(&all))
		return;
	ffstr_catfmt(&s, "time: %u.%06u.  ", (int)fftime_sec(&all), (int)fftime_usec(&all));

	FFARR_WALK(&t->filters, pf) {
		ffstr_catfmt(&s, "%s: %u.%06u (%u%%), "
			, pf->name, (int)fftime_sec(&pf->clk), (int)fftime_usec(&pf->clk)
			, (int)(fftime_mcs(&pf->clk) * 100 / fftime_mcs(&all)));
	}
	if (s.len > FFSLEN(", "))
		s.len -= FFSLEN(", ");

	dbglog(t, "%S", &s);
	ffarr_free(&s);
}

static void dict_ent_free(dict_ent *e)
{
	if (e->acq)
		ffmem_free(e->pval);
	ffmem_free(e);
}

static void trk_free(fm_trk *t)
{
	fmed_f *pf;
	dict_ent *e;
	fftree_node *node, *next;
	int type = t->props.type;

	core->task(&t->tsk, FMED_TASK_DEL);

	if (fmed->cmd.print_time) {
		struct ffps_perf i2 = {};
		ffps_perf(&i2, FFPS_PERF_REALTIME | FFPS_PERF_CPUTIME | FFPS_PERF_RUSAGE);
		ffps_perf_diff(&t->psperf, &i2);
		core->log(FMED_LOG_INFO, t, "track", "processing time: real:%u.%06u  cpu:%u.%06u (user:%u.%06u system:%u.%06u)"
			"  resources: pagefaults:%u  maxrss:%u  I/O:%u  ctxsw:%u"
			, (int)fftime_sec(&i2.realtime), (int)fftime_usec(&i2.realtime)
			, (int)fftime_sec(&i2.cputime), (int)fftime_usec(&i2.cputime)
			, (int)fftime_sec(&i2.usertime), (int)fftime_usec(&i2.usertime)
			, (int)fftime_sec(&i2.systime), (int)fftime_usec(&i2.systime)
			, i2.pagefaults, i2.maxrss, i2.inblock + i2.outblock, i2.vctxsw + i2.ivctxsw
			);
	}

	dbglog(t, "closing...");
	FFARR_RWALK(&t->filters, pf) {
		if (pf->ctx != NULL) {
			t->cur = &pf->sib;
			pf->filt->close(pf->ctx);
		}
	}

	if (core->loglev == FMED_LOG_DEBUG)
		trk_printtime(t);

	ffarr_free(&t->filters);

	FFTREE_WALKSAFE(&t->dict, node, next) {
		e = FF_GETPTR(dict_ent, nod, node);
		ffrbt_rm(&t->dict, &e->nod);
		dict_ent_free(e);
	}

	FFTREE_WALKSAFE(&t->meta, node, next) {
		e = FF_GETPTR(dict_ent, nod, node);
		ffrbt_rm(&t->meta, &e->nod);
		dict_ent_free(e);
	}

	if (fflist_exists(&fmed->trks, &t->sib))
		fflist_rm(&fmed->trks, &t->sib);

	dbglog(t, "closed");
	ffmem_free(t);

	if ((type == FMED_TRK_TYPE_REC && !fmed->cmd.gui)
		|| (fmed->stop_sig && fmed->trks.len == 0))
		core->sig(FMED_STOP);
}

#ifdef _DEBUG
// enum FMED_R
static const char *const fmed_retstr[] = {
	"err", "ok", "data", "more", "async", "done", "done-prev", "last-out", "fin", "syserr",
};
#endif

static int filt_call(fm_trk *t, fmed_f *f)
{
	int r;
	fftime t1, t2;

#ifdef _DEBUG
	dbglog(t, "%s calling %s, input: %L"
		, (f->newdata) ? ">>" : "<<", f->name, f->d.datalen);
#endif
	if (core->loglev == FMED_LOG_DEBUG) {
		ffclk_get(&t1);
	}

	ffint_bitmask(&t->props.flags, FMED_FFWD, f->newdata);
	f->newdata = 0;
	ffint_bitmask(&t->props.flags, FMED_FLAST, (t->cur->prev == ffchain_sentl(&t->filt_chain)));

	t->props.data = f->d.data,  t->props.datalen = f->d.datalen;

	if (!f->opened) {
		if (t->props.flags & FMED_FSTOP)
			return FMED_RFIN;

		dbglog(t, "creating context for %s...", f->name);
		f->ctx = f->filt->open(&t->props);
		f->d.data = t->props.data,  f->d.datalen = t->props.datalen;
		if (f->ctx == NULL) {
			t->state = TRK_ST_ERR;
			return FMED_RFIN;

		} else if (f->ctx == FMED_FILT_SKIP) {
			dbglog(t, "%s is skipped", f->name);
			f->ctx = NULL; //don't call fmed_filter.close()
			t->props.out = t->props.data,  t->props.outlen = t->props.datalen;
			return FMED_RDONE;
		}

		dbglog(t, "context for %s created", f->name);
		f->opened = 1;
	}

	r = f->filt->process(f->ctx, &t->props);
	f->d.data = t->props.data,  f->d.datalen = t->props.datalen;

	if (core->loglev == FMED_LOG_DEBUG) {
		ffclk_get(&t2);
		ffclk_diff(&t1, &t2);
		fftime_add(&f->clk, &t2);
	}

#ifdef _DEBUG
	dbglog(t, "   %s returned: %s, output: %L"
		, f->name, ((uint)(r + 1) < FFCNT(fmed_retstr)) ? fmed_retstr[r + 1] : "", t->props.outlen);
#endif
	return r;
}

static void trk_process(void *udata)
{
	fm_trk *t = udata;
	fmed_f *nf;
	fmed_f *f;
	int r, e;
	size_t ntasks = fmed->taskmgr.tasks.len;

	for (;;) {

		if (t->state != TRK_ST_ACTIVE) {
			if (t->state == TRK_ST_ERR)
				goto fin;
			return;
		}

		if (fmed->taskmgr.tasks.len != ntasks) {
			core->task(&t->tsk, FMED_TASK_POST);
			return;
		}

		f = FF_GETPTR(fmed_f, sib, t->cur);

		e = filt_call(t, f);

		switch (e) {
		case FMED_RSYSERR:
			syserrlog(core, t, f->name, "%s", "system error");
			// break
		case FMED_RERR:
			t->state = TRK_ST_ERR;
			goto fin;

		case FMED_RASYNC:
			return;

		case FMED_RMORE:
			r = FFLIST_CUR_PREV;
			break;

		case FMED_ROK:
			f->want_input = 1;
			r = FFLIST_CUR_NEXT;
			break;

		case FMED_RDATA:
			r = FFLIST_CUR_NEXT | FFLIST_CUR_SAMEIFBOUNCE;
			break;

		case FMED_RDONE:
			f->d.datalen = 0;
			r = FFLIST_CUR_NEXT | FFLIST_CUR_RM;
			break;

		case FMED_RDONE_PREV:
			f->d.datalen = 0;
			r = FFLIST_CUR_PREV | FFLIST_CUR_RM;
			break;

		case FMED_RLASTOUT:
			f->d.datalen = 0;
			r = FFLIST_CUR_NEXT | FFLIST_CUR_RM | FFLIST_CUR_RMPREV;
			break;

		case FMED_RFIN:
			goto fin;

		default:
			errlog(t, "unknown return code from module: %u", e);
			t->state = TRK_ST_ERR;
			goto fin;
		}

		if (f->d.datalen != 0)
			r |= FFLIST_CUR_SAMEIFBOUNCE;

shift:
		r = fflist_curshift(&t->cur, r | FFLIST_CUR_BOUNCE, ffchain_sentl(&t->filt_chain));

		switch (r) {
		case FFLIST_CUR_NONEXT:
			goto fin; //done

		case FFLIST_CUR_NOPREV:
			errlog(t, "module %s requires more input data", f->name);
			t->state = TRK_ST_ERR;
			goto fin;

		case FFLIST_CUR_NEXT:
			nf = FF_GETPTR(fmed_f, sib, t->cur);
			nf->d.data = t->props.out,  nf->d.datalen = t->props.outlen;
			t->props.outlen = 0;
			nf->newdata = 1;
			break;

		case FFLIST_CUR_SAME:
		case FFLIST_CUR_PREV:
			nf = FF_GETPTR(fmed_f, sib, t->cur);
			if (nf->want_input && nf->d.datalen == 0 && &nf->sib != ffchain_first(&t->filt_chain)) {
				nf->want_input = 0;
				r = FFLIST_CUR_PREV;
				f = nf;
				goto shift;
			}
			nf->want_input = 0;
			break;

		default:
			FF_ASSERT(0);
		}
	}

	return;

fin:
	if (t->state == TRK_ST_ERR)
		trk_setval(t, "error", 1);

	trk_free(t);
}


static dict_ent* dict_findstr(fm_trk *t, const ffstr *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_get(name->ptr, name->len);
	ffrbt_node *nod;

	nod = ffrbt_find(&t->dict, crc, NULL);
	if (nod == NULL)
		return NULL;
	ent = (dict_ent*)nod;

	if (!ffstr_eqz(name, ent->name))
		return NULL;

	return ent;
}

static dict_ent* dict_find(fm_trk *t, const char *name)
{
	ffstr s;
	ffstr_setz(&s, name);
	return dict_findstr(t, &s);
}

static dict_ent* dict_add(fm_trk *t, const char *name, uint *f)
{
	dict_ent *ent;
	uint crc = ffcrc32_getz(name, 0);
	ffrbt_node *nod, *parent;
	ffrbtree *tree = (*f & FMED_TRK_META) ? &t->meta : &t->dict;

	nod = ffrbt_find(tree, crc, &parent);
	if (nod != NULL) {
		ent = (dict_ent*)nod;
		if (0 != ffsz_cmp(name, ent->name)) {
			errlog(t, "setval: CRC collision: %u, key: %s, with key: %s"
				, crc, name, ent->name);
			t->state = TRK_ST_ERR;
			return NULL;
		}
		*f = 1;

	} else {
		ent = ffmem_tcalloc1(dict_ent);
		if (ent == NULL) {
			errlog(t, "setval: %e", FFERR_BUFALOC);
			t->state = TRK_ST_ERR;
			return NULL;
		}
		ent->nod.key = crc;
		ffrbt_insert(tree, &ent->nod, parent);
		ent->name = name;
		*f = 0;
	}

	return ent;
}

static dict_ent* meta_find(fm_trk *t, const ffstr *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_get(name->ptr, name->len);
	ffrbt_node *nod;

	nod = ffrbt_find(&t->meta, crc, NULL);
	if (nod == NULL)
		return NULL;
	ent = (dict_ent*)nod;

	if (!ffstr_eqz(name, ent->name))
		return NULL;

	return ent;
}

static int trk_meta_enum(fm_trk *t, fmed_trk_meta *meta)
{
	if (meta->trnod != &t->meta.sentl) {
		if (meta->trnod == NULL) {
			if (t->meta.root != &t->meta.sentl)
				meta->trnod = fftree_min((void*)t->meta.root, &t->meta.sentl);
			else
				meta->trnod = &t->meta.sentl;
		} else
			meta->trnod = fftree_successor(meta->trnod, &t->meta.sentl);
		if (meta->trnod != &t->meta.sentl) {
			const dict_ent *e = (dict_ent*)meta->trnod;
			ffstr_setz(&meta->name, e->name);
			ffstr_setz(&meta->val, e->pval);
			return 0;
		}
	}

	ffstr *val;
	if (meta->qent == NULL
		&& FMED_PNULL == (meta->qent = (void*)trk_getval(t, "queue_item")))
		return 1;
	for (;;) {
		val = fmed->qu->meta(meta->qent, meta->idx++, &meta->name, meta->flags);
		if (val == FMED_QUE_SKIP)
			continue;
		break;
	}
	if (val == NULL)
		return 1;
	meta->val = *val;
	return 0;
}

static int trk_meta_copy(fm_trk *t, fm_trk *src)
{
	fmed_trk_meta meta;
	ffmem_tzero(&meta);
	meta.flags = FMED_QUE_UNIQ;
	while (0 == trk_meta_enum(src, &meta)) {
		trk_setvalstr4(t, meta.name.ptr, (void*)&meta.val, FMED_TRK_META | FMED_TRK_VALSTR);
	}
	return 0;
}

/** Add filter to chain. */
static fmed_f* filt_add(fm_trk *t, uint cmd, const char *name)
{
	fmed_f *f;
	const void *iface;

	if (NULL == (iface = core->getmod2(FMED_MOD_IFACE, name, -1))) {
		errlog(t, "no such interface %s", name);
		return NULL;
	}

	switch (cmd) {
	case FMED_TRACK_ADDFILT_BEGIN:
		FF_ASSERT(t->state != TRK_ST_ACTIVE);
		if (NULL == ffarr_growT((ffarr*)&t->filters, 1, 4, fmed_f))
			return NULL;
		memmove(t->filters.ptr + 1, t->filters.ptr, t->filters.len * sizeof(fmed_f));
		f = t->filters.ptr;
		ffmem_tzero(f);
		break;

	case FMED_TRACK_ADDFILT:
		f = ffarr_end(&t->filters);
		ffmem_tzero(f);
		ffchain_append(&f->sib, t->cur);
		break;

	case FMED_TRACK_ADDFILT_PREV:
		f = ffarr_end(&t->filters);
		ffmem_tzero(f);
		ffchain_prepend(&f->sib, t->cur);
		break;
	}

	f->filt = iface;
	f->name = name;
	t->filters.len++;

	dbglog(t, "added %s to chain", f->name);
	return f;
}

static ssize_t trk_cmd(void *trk, uint cmd, ...)
{
	fm_trk *t = trk;
	fflist_item *next;
	ssize_t r = 0;
	va_list va;
	va_start(va, cmd);

	dbglog(NULL, "received command:%u, trk:%p", cmd, trk);

	switch (cmd) {
	case FMED_TRACK_STOPALL_EXIT:
		if (fmed->trks.len == 0 || fmed->stop_sig) {
			core->sig(FMED_STOP);
			break;
		}
		fmed->stop_sig = 1;
		trk = (void*)-1;
		// break

	case FMED_TRACK_STOPALL:
		FFLIST_WALKSAFE(&fmed->trks, t, sib, next) {
			if (t->props.type == FMED_TRK_TYPE_REC && trk == NULL)
				continue;

			trk_stop(t, cmd);
		}
		break;

	case FMED_TRACK_STOP:
		trk_stop(t, FMED_TRACK_STOP);
		break;

	case FMED_TRACK_START:
		if (0 != trk_setout(t)) {
			trk_setval(t, "error", 1);
		}
		if (0 != trk_opened(t)) {
			trk_free(t);
			r = -1;
			break;
		}

		if (fmed->cmd.print_time)
			ffps_perf(&t->psperf, FFPS_PERF_REALTIME | FFPS_PERF_CPUTIME | FFPS_PERF_RUSAGE);

		trk_process(t);
		break;

	case FMED_TRACK_PAUSE:
		t->state = TRK_ST_PAUSED;
		break;
	case FMED_TRACK_UNPAUSE:
		t->state = TRK_ST_ACTIVE;
		trk_process(t);
		break;

	case FMED_TRACK_LAST:
		if (!fmed->cmd.gui && !fmed->cmd.rec)
			core->sig(FMED_STOP);
		break;

	case FMED_TRACK_ADDFILT:
	case FMED_TRACK_ADDFILT_PREV:
	case FMED_TRACK_ADDFILT_BEGIN: {
		const char *name = va_arg(va, char*);
		fmed_f *f = filt_add(t, cmd, name);
		if (f == NULL) {
			r = -1;
			break;
		}
		r = 0;
		break;
	}

	case FMED_TRACK_FILT_GETPREV: {
		if (t->cur->prev == ffchain_sentl(&t->filt_chain)) {
			r = -1;
			break;
		}
		fmed_f *f = FF_GETPTR(fmed_f, sib, t->cur->prev);
		void **dst = va_arg(va, void**);
		*dst = f->ctx;
		break;
	}

	case FMED_TRACK_META_HAVEUSER: {
		if (t->meta.len != 0) {
			r = 1;
			break;
		}
		void *qent;
		if (FMED_PNULL == (qent = (void*)trk_getval(t, "queue_item"))) {
			r = 0;
			break;
		}
		r = fmed->qu->cmd2(FMED_QUE_HAVEUSERMETA, qent, 0);
		break;
	}

	case FMED_TRACK_META_ENUM: {
		fmed_trk_meta *meta = va_arg(va, void*);
		r = trk_meta_enum(t, meta);
		break;
	}

	case FMED_TRACK_META_COPYFROM: {
		fm_trk *src = va_arg(va, void*);
		r = trk_meta_copy(t, src);
		break;
	}

	default:
		errlog(t, "invalid command:%u", cmd);
	}

	va_end(va);
	return r;
}

static int trk_cmd2(void *trk, uint cmd, void *param)
{
	return trk_cmd(trk, cmd, param);
}

static void trk_loginfo(void *trk, const ffstr **id, const char **module)
{
	fm_trk *t = trk;
	*id = &t->id;
	*module = NULL;
	if (t->cur != NULL) {
		const fmed_f *f = FF_GETPTR(fmed_f, sib, t->cur);
		*module = f->name;
	}
}

static int64 trk_popval(void *trk, const char *name)
{
	fm_trk *t = trk;
	dict_ent *ent = dict_find(t, name);
	if (ent != NULL) {
		int64 val = ent->val;
		ffrbt_rm(&t->dict, &ent->nod);
		dict_ent_free(ent);
		return val;
	}

	return FMED_NULL;
}

static int64 trk_getval(void *trk, const char *name)
{
	fm_trk *t = trk;
	dict_ent *ent = dict_find(t, name);
	if (ent != NULL)
		return ent->val;
	return FMED_NULL;
}

static const char* trk_getvalstr(void *trk, const char *name)
{
	fm_trk *t = trk;
	dict_ent *ent = dict_find(t, name);
	if (ent != NULL)
		return ent->pval;
	return FMED_PNULL;
}

static char* trk_getvalstr3(void *trk, const void *name, uint flags)
{
	fm_trk *t = trk;
	dict_ent *ent;
	ffstr nm;

	if (flags & FMED_TRK_NAMESTR)
		ffstr_set2(&nm, (ffstr*)name);
	else
		ffstr_setz(&nm, (char*)name);

	if (flags & FMED_TRK_META) {
		ent = meta_find(t, &nm);
		if (ent == NULL) {
			void *qent;
			if (FMED_PNULL == (qent = (void*)trk_getval(t, "queue_item")))
				return FMED_PNULL;
			ffstr *val;
			if (NULL == (val = fmed->qu->meta_find(qent, nm.ptr, nm.len)))
				return FMED_PNULL;
			if (flags & FMED_TRK_VALSTR)
				return (void*)val;
			return val->ptr;
		}
	} else
		ent = dict_findstr(t, &nm);
	if (ent == NULL)
		return FMED_PNULL;

	return ent->pval;
}

static int trk_setval(void *trk, const char *name, int64 val)
{
	trk_setval4(trk, name, val, 0);
	return 0;
}

static int64 trk_setval4(void *trk, const char *name, int64 val, uint flags)
{
	fm_trk *t = trk;
	uint st = 0;
	dict_ent *ent = dict_add(t, name, &st);
	if (ent == NULL)
		return FMED_NULL;

	if ((flags & FMED_TRK_FNO_OVWRITE) && st == 1)
		return ent->val;

	if (ent->acq) {
		ffmem_free(ent->pval);
		ent->acq = 0;
	}

	ent->val = val;
	dbglog(trk, "setval: %s = %D", name, val);
	return val;
}

static char* trk_setvalstr4(void *trk, const char *name, const char *val, uint flags)
{
	fm_trk *t = trk;
	dict_ent *ent;
	uint st = flags;

	if (flags & FMED_TRK_META) {
		ent = dict_add(t, name, &st);
		if (ent == NULL)
			return NULL;

		if (ent->acq)
			ffmem_free(ent->pval);

		if (flags & FMED_TRK_VALSTR) {
			const ffstr *sval = (void*)val;
			ent->pval = ffsz_alcopy(sval->ptr, sval->len);
		} else
			ent->pval = ffsz_alcopyz(val);

		if (ent->pval == NULL)
			return NULL;

		ent->acq = 1;
		dbglog(trk, "set meta: %s = %s", name, ent->pval);
		return ent->pval;

	} else
		ent = dict_add(t, name, &st);

	if (ent == NULL
		|| ((flags & FMED_TRK_FNO_OVWRITE) && st == 1)) {

		if (flags & FMED_TRK_FACQUIRE)
			ffmem_free((char*)val);
		return (ent != NULL) ? ent->pval : NULL;
	}

	if (ent->acq)
		ffmem_free(ent->pval);
	ent->acq = (flags & FMED_TRK_FACQUIRE) ? 1 : 0;

	ent->pval = (void*)val;

	dbglog(trk, "setval: %s = %s", name, val);
	return ent->pval;
}

static int trk_setvalstr(void *trk, const char *name, const char *val)
{
	trk_setvalstr4(trk, name, val, 0);
	return 0;
}
