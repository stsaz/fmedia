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
	N_FILTERS = 32, //allow up to this number of filters to be added while track is running
};

struct tracks {
	ffatomic trkid;
	fflist trks; //fm_trk[]
	const struct fmed_trk_mon *mon;
	uint stop_sig :1;
};

static struct tracks *g;

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
static char* chain_print(fm_trk *t, const ffchain_item *mark, char *buf, size_t cap);

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
static void trk_meta_set(void *trk, const ffstr *name, const ffstr *val, uint flags);
const fmed_track _fmed_track = {
	&trk_create, &trk_conf, &trk_copy_info, &trk_cmd, &trk_cmd2,
	&trk_popval, &trk_getval, &trk_getvalstr, &trk_setval, &trk_setvalstr, &trk_setval4, &trk_setvalstr4, &trk_getvalstr3,
	&trk_loginfo,
	&trk_meta_set,
};


int tracks_init(void)
{
	if (NULL == (g = ffmem_new(struct tracks)))
		return -1;
	fflist_init(&g->trks);
	return 0;
}

void tracks_destroy(void)
{
	if (g == NULL)
		return;
	trk_cmd(NULL, FMED_TRACK_STOPALL);
	ffmem_free0(g);
}

static fmed_f* addfilter1(fm_trk *t, const fmed_modinfo *mod)
{
	return filt_add(t, FMED_TRACK_FILT_ADDLAST, mod->name);
}

static fmed_f* addfilter(fm_trk *t, const char *modname)
{
	return filt_add(t, FMED_TRACK_FILT_ADDLAST, modname);
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

	if (t->props.a_start_level != 0)
		addfilter(t, "#soundmod.startlevel");

	if (t->props.a_stop_level != 0)
		addfilter(t, "#soundmod.stoplevel");

	if (t->props.type == FMED_TRK_TYPE_REC && t->props.a_prebuffer != 0) {
		addfilter(t, "#soundmod.membuf");
	}

	if (t->props.type != FMED_TRK_TYPE_MIXOUT && !stream_copy) {
		addfilter(t, "#soundmod.gain");
	}

	if (t->props.use_dynanorm)
		addfilter(t, "dynanorm.filter");

	addfilter(t, "#soundmod.autoconv");

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
	if (core->loglev == FMED_LOG_DEBUG) {
		dbglog(t, "properties: %*xb", sizeof(t->props), &t->props);
	}

	fflist_ins(&g->trks, &t->sib);
	t->state = TRK_ST_ACTIVE;
	t->cur = ffchain_first(&t->filt_chain);
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

	t->id.len = ffs_fmt(t->sid, t->sid + sizeof(t->sid), "*%L", ffatom_incret(&g->trkid));
	t->id.ptr = t->sid;

	if (NULL == ffarr_allocT((ffarr*)&t->filters, N_FILTERS, fmed_f))
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
	dst->a_prebuffer = src->a_prebuffer;
	dst->a_start_level = src->a_start_level;
	dst->a_stop_level = src->a_stop_level;
	dst->a_stop_level_time = src->a_stop_level_time;
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

	if (fflist_exists(&g->trks, &t->sib))
		fflist_rm(&g->trks, &t->sib);

	if (g->mon != NULL)
		g->mon->onsig(&t->props, FMED_TRK_ONCLOSE);

	dbglog(t, "closed");
	ffmem_free(t);

	if (g->stop_sig && g->trks.len == 0)
		core->sig(FMED_STOP);
}

#ifdef _DEBUG
// enum FMED_R
static const char *const fmed_retstr[] = {
	"err", "ok", "data", "done", "last-out",
	"more", "back",
	"async", "fin", "syserr",
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

		dbglog(t, "context for %s created: 0x%p", f->name, f->ctx);
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
	size_t jobdata;
	core_job_enter(0, &jobdata);

	for (;;) {

		if (t->state != TRK_ST_ACTIVE) {
			if (t->state == TRK_ST_ERR)
				goto fin;
			return;
		}

		if (core_job_shouldyield(0, &jobdata)) {
			trk_cmd(t, FMED_TRACK_WAKE);
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
			FF_ASSERT(t->props.outlen == 0);
			r = FFLIST_CUR_PREV;
			break;

		case FMED_RBACK:
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
			if (e == FMED_RBACK) {
				nf->d.data = t->props.out,  nf->d.datalen = t->props.outlen;
				nf->newdata = 1;
			}
			t->props.outlen = 0;
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

static void trk_meta_set(void *trk, const ffstr *name, const ffstr *val, uint flags)
{
	fm_trk *t = trk;
	void *qent = (void*)trk_getval(t, "queue_item");
	if (qent == FMED_PNULL)
		return;
	fmed->qu->meta_set(qent, name->ptr, name->len, val->ptr, val->len, flags);
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
	if (ffarr_isfull(&t->filters)) {
		errlog(t, "can't add more filters", 0);
		return NULL;
	}

	f = ffarr_endT(&t->filters, fmed_f);
	ffmem_tzero(f);
	if (NULL == (f->filt = core->getmod2(FMED_MOD_IFACE, name, -1))) {
		errlog(t, "no such interface %s", name);
		return NULL;
	}

	switch (cmd) {
	case FMED_TRACK_FILT_ADDFIRST:
	case FMED_TRACK_ADDFILT_BEGIN:
		ffchain_addfront(&t->filt_chain, &f->sib);
		break;

	case FMED_TRACK_FILT_ADDLAST:
		ffchain_add(&t->filt_chain, &f->sib);
		break;

	case FMED_TRACK_FILT_ADD:
	case FMED_TRACK_ADDFILT:
		ffchain_append(&f->sib, t->cur);
		break;

	case FMED_TRACK_FILT_ADDPREV:
	case FMED_TRACK_ADDFILT_PREV:
		ffchain_prepend(&f->sib, t->cur);
		break;
	}

	f->name = name;
	t->filters.len++;

	if (t->cur == ffchain_sentl(&t->filt_chain))
		t->cur = ffchain_first(&t->filt_chain);

	char buf[255];
	dbglog(t, "added %s to chain [%s]"
		, f->name, chain_print(t, &f->sib, buf, sizeof(buf)));
	return f;
}

/** Print names of all filters in chain. */
static char* chain_print(fm_trk *t, const ffchain_item *mark, char *buf, size_t cap)
{
	FF_ASSERT(cap != 0);
	char *p = buf, *end = buf + cap - 1;
	ffchain_item *it;
	fmed_f *f;

	FFCHAIN_WALK(&t->filt_chain, it) {
		f = FF_GETPTR(fmed_f, sib, it);
		p += ffs_fmt(p, end, (it == mark) ? "*%s -> " : "%s -> ", f->name);
	}

	*p = '\0';
	return buf;
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
		if (g->trks.len == 0 || g->stop_sig) {
			core->sig(FMED_STOP);
			break;
		}
		g->stop_sig = 1;
		trk = (void*)-1;
		// break

	case FMED_TRACK_STOPALL:
		FFLIST_WALKSAFE(&g->trks, t, sib, next) {
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

		core->task(&t->tsk, FMED_TASK_POST);
		break;

	case FMED_TRACK_PAUSE:
		t->state = TRK_ST_PAUSED;
		break;
	case FMED_TRACK_UNPAUSE:
		t->state = TRK_ST_ACTIVE;
		trk_process(t);
		break;

	case FMED_TRACK_LAST:
		if (g->mon != NULL)
			g->mon->onsig(&t->props, FMED_TRK_ONLAST);
		break;

	case FMED_TRACK_WAKE:
		core->task(&t->tsk, FMED_TASK_POST);
		break;

	case FMED_TRACK_FILT_ADDFIRST:
	case FMED_TRACK_FILT_ADDLAST:
	case FMED_TRACK_FILT_ADD:
	case FMED_TRACK_FILT_ADDPREV: {
		const char *name = va_arg(va, char*);
		fmed_f *f = filt_add(t, cmd, name);
		r = (size_t)f;
		break;
	}
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

	case FMED_TRACK_FILT_INSTANCE: {
		fmed_f *f = va_arg(va, void*);
		if (!f->opened) {
			dbglog(t, "creating instance of %s...", f->name);
			f->ctx = f->filt->open(&t->props);
			FF_ASSERT(f->ctx != NULL);
			if (f->ctx == NULL)
				t->state = TRK_ST_ERR;
			f->opened = 1;
		}
		r = (size_t)f->ctx;
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

	case FMED_TRACK_MONITOR:
		g->mon = va_arg(va, void*);
		break;

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
