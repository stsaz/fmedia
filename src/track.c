/** fmedia track.
Copyright (c) 2016 Simon Zolin */

#include <core.h>

#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/path.h>
#include <FF/time.h>
#include <FF/sys/filemap.h>
#include <FFOS/error.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>
#include <ffbase/murmurhash3.h>


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
	const fmed_queue *qu;
	uint stop_sig :1;
	uint last :1;
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

		/** This filter won't return any more data, it won't be called again.
		However, it is still in chain while the next filters use its data. */
		, done :1

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
	ffchain_item chain_parent;
	struct {FFARR(fmed_f)} filters;
	fflist_cursor cur;
	ffrbtree dict;
	ffrbtree meta;
	struct ffps_perf psperf;
	fftask tsk, tsk_stop, tsk_main;
	uint wid; //associated worker ID
	fffd kq; //worker's kqueue

	ffstr id;
	char sid[FFSLEN("*") + FFINT_MAXCHARS];

	uint state; //enum TRK_ST
	uint wflags;
} fm_trk;


static int trk_setout_file(fm_trk *t);
static int trk_setout(fm_trk *t);
static int trk_opened(fm_trk *t);
static int trk_open(fm_trk *t, const char *fn);
static void trk_open_capt(fm_trk *t);
static void trk_free(fm_trk *t);
static void trk_fin(fm_trk *t);
static void trk_process(void *udata);
static void trk_stop(fm_trk *t, uint flags);
static fmed_f* trk_modbyext(fm_trk *t, uint flags, const ffstr *ext);
static void trk_printtime(fm_trk *t);
static int trk_meta_enum(fm_trk *t, fmed_trk_meta *meta);
static int trk_meta_copy(fm_trk *t, fm_trk *src);
static char* chain_print(fm_trk *t, const ffchain_item *mark, char *buf, size_t cap);

static fmed_f* addfilter(fm_trk *t, const char *modname);
static fmed_f* addfilter1(fm_trk *t, const fmed_modinfo *mod);
static fmed_f* filt_add(fm_trk *t, uint cmd, const char *name);
static int filt_call(fm_trk *t, fmed_f *f);
static void filt_close(fm_trk *t, fmed_f *f);

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
	g->qu = core->getmod("#queue.queue");
	fflist_init(&g->trks);
	return 0;
}

void tracks_destroy(void)
{
	if (g == NULL)
		return;
	g->stop_sig = 0;
	fm_trk *t;
	fflist_item *next;
	FFLIST_WALKSAFE(&g->trks, t, sib, next) {
		trk_free(t);
	}
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

/** Don't print error message on failure. */
static fmed_f* filt_add_optional(fm_trk *t, const char *modname)
{
	return filt_add(t, FMED_TRACK_FILT_ADDLAST | 0x80000000, modname);
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
	filt_add_optional(t, "#winsleep.sleep");
	filt_add_optional(t, "dbus.sleep");
	addfilter(t, "#queue.track");

	if (0 == fffile_infofn(fn, &fi) && fffile_isdir(fffile_infoattr(&fi))) {
		addfilter(t, "plist.dir");
		return 0;
	}

	if (ffsz_matchz(fn, "http://")) {
		addfilter(t, "net.http");
	} else {
		uint have_path = (NULL != ffpath_split2(fn, ffsz_len(fn), NULL, &name));
		ffpath_splitname(name.ptr, name.len, &name, &ext);
		if (!have_path && ffstr_eqcz(&name, "@stdin"))
			addfilter(t, "#file.stdin");
		else
			addfilter(t, "#file.in");

		if (NULL == trk_modbyext(t, FMED_MOD_INEXT, &ext)) {
			errlog(t, "can't open file: \"%s\"", fn);
			return 1;
		}
	}

	return 0;
}

static void trk_open_capt(fm_trk *t)
{
	filt_add_optional(t, "#winsleep.sleep");
	filt_add_optional(t, "dbus.sleep");
	ffpcm_fmtcopy(&t->props.audio.fmt, &core->props->record_format);

	if (core->props->record_format.channels & ~FFPCM_CHMASK) {
		t->props.audio.convfmt.channels = core->props->record_format.channels;
		t->props.audio.fmt.channels = core->props->record_format.channels & FFPCM_CHMASK;
	}

	addfilter1(t, core->props->record_module);

	addfilter(t, "#soundmod.until");
	addfilter(t, "#soundmod.rtpeak");
}

static int trk_setout(fm_trk *t)
{
	const char *s;
	ffbool stream_copy = t->props.stream_copy;

	switch (t->props.type) {
	case FMED_TRK_TYPE_EXPAND:
		return 0;

	case FMED_TRK_TYPE_PLIST:
		if (0 != trk_setout_file(t))
			return 1;
		return 0;
	}

	if (t->props.type == FMED_TRK_TYPE_NETIN) {

	} else if (t->props.type == FMED_TRK_TYPE_NONE) {
		return 0;

	} else if (t->props.type != FMED_TRK_TYPE_MIXIN) {
		if (t->props.type != FMED_TRK_TYPE_REC)
			addfilter(t, "#soundmod.until");
		if (core->props->gui)
			addfilter(t, "gui.gui");
		else if (core->props->tui)
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
		ffbool playback = (t->props.type == FMED_TRK_TYPE_PLAYBACK
			&& FMED_PNULL == (s = trk_getvalstr(t, "output"))
			&& core->props->playback_module != NULL);

		if (!(playback && t->props.audio.auto_attenuate_ceiling != 0.0))
			addfilter(t, "#soundmod.gain");
	}

	if (t->props.use_dynanorm)
		addfilter(t, "dynanorm.filter");

	if ((int64)t->props.audio.split != FMED_NULL) {
		addfilter(t, "#soundmod.split");
		return 0;
	}

	addfilter(t, "#soundmod.autoconv");

	if (t->props.type == FMED_TRK_TYPE_MIXIN) {
		addfilter(t, "mixer.in");

	} else if (t->props.pcm_peaks) {
		addfilter(t, "#soundmod.peaks");

	} else if (FMED_PNULL != (s = trk_getvalstr(t, "output"))) {
		if (0 != trk_setout_file(t))
			return -1;

	} else if (core->props->playback_module != NULL) {
		addfilter(t, "#soundmod.auto-attenuator");
		addfilter1(t, core->props->playback_module);
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

	case FMED_TRK_TYPE_EXPAND:
		t->props.input_info = 1;
		// fallthrough

	case FMED_TRK_TYPE_PLAYBACK:
		if (0 != trk_open(t, fn)) {
			trk_free(t);
			return FMED_TRK_EFMT;
		}
		break;

	case FMED_TRK_TYPE_REC:
		trk_open_capt(t);
		break;

	case FMED_TRK_TYPE_MIXOUT:
		addfilter(t, "#queue.track");
		addfilter(t, "mixer.out");
		break;

	case FMED_TRK_TYPE_PLIST:
		break;

	default:
		if (cmd >= _FMED_TRK_TYPE_END) {
			errlog(t, "unknown track type:%u", cmd);
			goto err;
		}
		break;
	}
	t->props.type = cmd;

	return t;

err:
	trk_free(t);
	return NULL;
}

/** Set output module and file. */
static int trk_setout_file(fm_trk *t)
{
	const char *ofn = trk_getvalstr(t, "output");
	ffstr name, ext;
	ffbool have_path = (NULL != ffpath_split2(ofn, ffsz_len(ofn), NULL, &name));
	ffstr_rsplitby(&name, '.', &name, &ext);
	if (NULL == trk_modbyext(t, FMED_MOD_OUTEXT, &ext))
		return 1;

	if (!have_path && ffstr_eqcz(&name, "@stdout")) {
		addfilter(t, "#file.stdout");
		t->props.out_seekable = 0;
	} else {
		addfilter(t, "#file.out");
		t->props.out_seekable = 1;
	}
	return 0;
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
		dst->audio.split = FMED_NULL;
		memset(&dst->_bar_start, 0xff, FFOFF(fmed_trk, _bar_end) - FFOFF(fmed_trk, _bar_start));
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
	dst->a_stop_level_mintime = src->a_stop_level_mintime;
	dst->include_files = src->include_files;
	dst->exclude_files = src->exclude_files;
	dst->bits = src->bits;
}

/** Stop the track.  Thread: worker. */
static void trk_onstop(void *p)
{
	fm_trk *t = p;
	trk_setval(t, "stopped", 1);
	t->props.flags |= FMED_FSTOP;
	if (t->state != TRK_ST_ACTIVE)
		trk_fin(t);
}

/** Submit track stop event. */
static void trk_stop(fm_trk *t, uint flags)
{
	fftask_set(&t->tsk_stop, &trk_onstop, t);
	core->cmd(FMED_TASK_XPOST, &t->tsk_stop, t->wid);
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

static void trk_free_tsk(void *param)
{
	trk_free(param);
}

/** Finish processing for the track.  Thread: worker. */
static void trk_fin(fm_trk *t)
{
	fftask_set(&t->tsk_main, &trk_free_tsk, t);
	core->task(&t->tsk_main, FMED_TASK_POST);
}

/** Free memory associated with the track.  Thread: main. */
static void trk_free(fm_trk *t)
{
	fmed_f *pf;

	dbglog(t, "closing...");
	core->task(&t->tsk_main, FMED_TASK_DEL);
	core->cmd(FMED_TASK_XDEL, &t->tsk, t->wid);
	core->cmd(FMED_TASK_XDEL, &t->tsk_stop, t->wid);
	if (t->state == TRK_ST_ERR)
		t->props.err = 1;

	if (t->props.print_time) {
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

	FFARR_RWALK(&t->filters, pf) {
		if (pf->ctx != NULL) {
			t->cur = &pf->sib;
			dbglog(t, "closing %s", pf->name);
			pf->filt->close(pf->ctx);
		}
	}
	t->cur = NULL;

	if (core->loglev == FMED_LOG_DEBUG)
		trk_printtime(t);

	ffarr_free(&t->filters);

	ffrbt_freeall(&t->dict, (ffrbt_free_t)&dict_ent_free, FFOFF(dict_ent, nod));
	ffrbt_freeall(&t->meta, (ffrbt_free_t)&dict_ent_free, FFOFF(dict_ent, nod));

	if (fflist_exists(&g->trks, &t->sib)) {
		fflist_rm(&g->trks, &t->sib);
		core->cmd(FMED_WORKER_RELEASE, t->wid, t->wflags);
	}

	if (g->mon != NULL) {
		g->mon->onsig(t, FMED_TRK_ONCLOSE);
		if (g->last && g->trks.len == 0)
			g->mon->onsig(t, FMED_TRK_ONLAST);
	}

	dbglog(t, "closed");
	ffmem_free(t);

	if (g->stop_sig && g->trks.len == 0)
		core->sig(FMED_STOP);
}

/** Return TRUE if:
 . if it's the first filter
 . if all previous filters are marked as "done" */
static int filt_isfirst(fm_trk *t, fflist_item *item)
{
	for (fflist_item *it = item->prev;  ;  it = it->prev) {
		if (it == ffchain_sentl(&t->filt_chain))
			return 1;

		fmed_f *f = FF_GETPTR(fmed_f, sib, it);
		if (!f->done)
			return 0;
	}
	return 0;
}

static int filt_islast(fm_trk *t, fflist_item *item)
{
	return (item->next == ffchain_sentl(&t->filt_chain));
}

// enum FMED_R
static const char *const fmed_retstr[] = {
	"FMED_RERR",
	"FMED_ROK",
	"FMED_RDATA",
	"FMED_RDONE",
	"FMED_RLASTOUT",
	"FMED_RNEXTDONE",
	"FMED_RMORE",
	"FMED_RBACK",
	"FMED_RASYNC",
	"FMED_RFIN",
	"FMED_RSYSERR",
	"FMED_RDONE_ERR",
};

static int filt_call(fm_trk *t, fmed_f *f)
{
	int r;
	fftime t1 = {}, t2;

	if (core->loglev == FMED_LOG_DEBUG) {
		ffclk_get(&t1);
	}

	ffint_bitmask(&t->props.flags, FMED_FFWD, f->newdata);
	f->newdata = 0;

	ffbool first = filt_isfirst(t, t->cur);
	ffint_bitmask(&t->props.flags, FMED_FLAST, first);

	dbglog(t, "%s calling %s, input:%L  flags:%xu"
		, (t->props.flags & FMED_FFWD) ? ">>" : "<<", f->name, f->d.datalen, t->props.flags);

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

	dbglog(t, "   %s returned: %s, output:%L"
		, f->name, ((uint)(r + 1) < FFCNT(fmed_retstr)) ? fmed_retstr[r + 1] : "", t->props.outlen);
	return r;
}

static void trk_process(void *udata)
{
	fm_trk *t = udata;
	fmed_f *nf;
	fmed_f *f;
	int r, e;
	size_t jobdata;
	core_job_enter(t->wid, &jobdata);

	for (;;) {

		if (t->state != TRK_ST_ACTIVE) {
			if (t->state == TRK_ST_ERR)
				goto fin;
			return;
		}

		if (core_job_shouldyield(t->wid, &jobdata)) {
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
			r = FFLIST_CUR_NEXT | FFLIST_CUR_BOUNCE;
			if (f->d.datalen != 0)
				r |= FFLIST_CUR_SAMEIFBOUNCE;
			break;

		case FMED_RDATA:
			r = FFLIST_CUR_NEXT | FFLIST_CUR_BOUNCE | FFLIST_CUR_SAMEIFBOUNCE;
			break;

		case FMED_RDONE_ERR:
			trk_setval(t, "error", 1);
			t->props.err = 1;
			// fallthrough
		case FMED_RDONE:
			if (filt_islast(t, t->cur)) {
				filt_close(t, f);
				r = FFLIST_CUR_NEXT | FFLIST_CUR_BOUNCE | FFLIST_CUR_RM;
			} else {
				// close filter after the next filters are done with its data
				f->done = 1;
				r = FFLIST_CUR_NEXT;
			}
			break;

		case FMED_RLASTOUT:
			f->d.datalen = 0;
			r = FFLIST_CUR_NEXT | FFLIST_CUR_BOUNCE | FFLIST_CUR_RM | FFLIST_CUR_RMPREV;
			break;

		case FMED_RNEXTDONE: {
			FF_ASSERT(t->chain_parent.next == NULL); //only one NEXTDONE is supported per chain
			FF_ASSERT(t->cur->next != ffchain_sentl(&t->filt_chain));

			ffchain_item *nxt = t->cur->next;
			ffchain_split(t->cur, ffchain_sentl(&t->filt_chain));
			t->chain_parent.next = ffchain_first(&t->filt_chain);
			t->chain_parent.prev = t->cur;
			t->filt_chain.next = nxt;
			t->cur = nxt;

			nf = FF_GETPTR(fmed_f, sib, t->cur);
			nf->d.data = t->props.out,  nf->d.datalen = t->props.outlen;
			t->props.outlen = 0;
			nf->newdata = 1;
			continue;
		}

		case FMED_RFIN:
			goto fin;

		default:
			errlog(t, "unknown return code from module: %u", e);
			t->state = TRK_ST_ERR;
			goto fin;
		}

shift:
		r = fflist_curshift(&t->cur, r, ffchain_sentl(&t->filt_chain));

		switch (r) {
		case FFLIST_CUR_NONEXT:
			if (t->chain_parent.next != NULL) {
				t->filt_chain = t->chain_parent;
				t->chain_parent.next = t->chain_parent.prev = NULL;
				t->cur = ffchain_last(&t->filt_chain);
				break;
			}
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
			if (filt_islast(t, &f->sib)
				&& (e == FMED_RDATA || e == FMED_ROK)) {
				errlog(t, "module %s, the last in chain, outputs more data", f->name);
				t->state = TRK_ST_ERR;
				goto fin;
			}
			// fall through

		case FFLIST_CUR_PREV:
			nf = FF_GETPTR(fmed_f, sib, t->cur);

			if (nf->done) {
				t->props.outlen = 0;
				filt_close(t, nf);
				if (filt_isfirst(t, t->cur)) {
					r = FFLIST_CUR_NEXT | FFLIST_CUR_RM | FFLIST_CUR_BOUNCE;
					goto shift;
				}
				r = FFLIST_CUR_PREV | FFLIST_CUR_RM | FFLIST_CUR_BOUNCE;
				goto shift;
			}

			if (e == FMED_RBACK) {
				nf->d.data = t->props.out,  nf->d.datalen = t->props.outlen;
				nf->newdata = 1;
			}
			t->props.outlen = 0;
			if (nf->want_input && nf->d.datalen == 0 && !filt_isfirst(t, t->cur)) {
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

	trk_fin(t);
}


static dict_ent* dict_findstr(fm_trk *t, const ffstr *name)
{
	dict_ent *ent;
	uint crc = murmurhash3(name->ptr, name->len, 0x12345678);
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
	uint crc = murmurhash3(name, ffsz_len(name), 0x12345678);
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
			syserrlog(core, t, "track", "mem alloc", 0);
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
	g->qu->meta_set(qent, name->ptr, name->len, val->ptr, val->len, flags);
}

static dict_ent* meta_find(fm_trk *t, const ffstr *name)
{
	dict_ent *ent;
	uint crc = murmurhash3(name->ptr, name->len, 0x12345678);
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
		val = g->qu->meta(meta->qent, meta->idx++, &meta->name, meta->flags);
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
	uint optional = cmd & 0x80000000;
	cmd &= ~0x80000000;
	fmed_f *f;
	if (ffarr_isfull(&t->filters)) {
		if (!optional)
			errlog(t, "can't add more filters", 0);
		return NULL;
	}

	f = ffarr_endT(&t->filters, fmed_f);
	ffmem_tzero(f);
	if (NULL == (f->filt = core->getmod2(FMED_MOD_IFACE | FMED_MOD_NOLOG, name, -1))) {
		if (!optional)
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

/** Close filter context. */
static void filt_close(fm_trk *t, fmed_f *f)
{
	char buf[255];
	dbglog(t, "closing %s in chain [%s]"
		, f->name, chain_print(t, &f->sib, buf, sizeof(buf)));

	if (f->ctx != NULL) {
		f->filt->close(f->ctx);
		f->ctx = NULL;
	}

	uint n = 0;
	FFARR_RWALKT(&t->filters, f, fmed_f) {
		if (f->ctx != NULL)
			break;
		n++;
	}
	t->filters.len -= n;
}

/** Print names of all filters in chain. */
static char* chain_print(fm_trk *t, const ffchain_item *mark, char *buf, size_t cap)
{
	FF_ASSERT(cap != 0);
	char *p = buf, *end = buf + cap - 1;
	ffchain_item *it;
	fmed_f *f;

	if (t->chain_parent.next != NULL) {
		FFCHAIN_WALK(&t->chain_parent, it) {
			if (it == ffchain_sentl(&t->filt_chain))
				break;
			f = FF_GETPTR(fmed_f, sib, it);
			p += ffs_fmt(p, end, (it == mark) ? "*%s -> " : "%s -> ", f->name);
		}
	}

	FFCHAIN_WALK(&t->filt_chain, it) {
		f = FF_GETPTR(fmed_f, sib, it);
		p += ffs_fmt(p, end, (it == mark) ? "*%s -> " : "%s -> ", f->name);
	}

	*p = '\0';
	return buf;
}

static const char* const cmd_str[] = {
	"FMED_TRACK_START",
	"FMED_TRACK_STOP",
	"FMED_TRACK_PAUSE",
	"FMED_TRACK_UNPAUSE",
	"FMED_TRACK_STOPALL",
	"FMED_TRACK_STOPALL_EXIT",
	"FMED_TRACK_LAST",
	"FMED_TRACK_ADDFILT",
	"FMED_TRACK_ADDFILT_PREV",
	"FMED_TRACK_ADDFILT_BEGIN",
	"FMED_TRACK_FILT_ADD",
	"FMED_TRACK_FILT_ADDPREV",
	"FMED_TRACK_FILT_ADDFIRST",
	"FMED_TRACK_FILT_ADDLAST",
	"FMED_TRACK_META_HAVEUSER",
	"FMED_TRACK_META_ENUM",
	"FMED_TRACK_META_COPYFROM",
	"FMED_TRACK_FILT_GETPREV",
	"FMED_TRACK_FILT_INSTANCE",
	"FMED_TRACK_WAKE",
	"FMED_TRACK_MONITOR",
	"FMED_TRACK_KQ",
	"FMED_TRACK_XSTART",
};

static ssize_t trk_cmd(void *trk, uint cmd, ...)
{
	fm_trk *t = trk;
	fflist_item *next;
	ssize_t r = 0;
	va_list va;
	va_start(va, cmd);

	dbglog(NULL, "received command:%s, trk:%p"
		, (cmd < FF_COUNT(cmd_str)) ? cmd_str[cmd] : "", trk);

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
		FF_ASSERT(core_ismainthr());
		FFLIST_WALKSAFE(&g->trks, t, sib, next) {
			if (t->props.type == FMED_TRK_TYPE_REC && trk == NULL)
				continue;

			trk_stop(t, cmd);
		}
		break;

	case FMED_TRACK_STOP:
		FF_ASSERT(core_ismainthr());
		trk_stop(t, FMED_TRACK_STOP);
		break;

	case FMED_TRACK_START:
	case FMED_TRACK_XSTART:
		if (0 != trk_setout(t)) {
			trk_setval(t, "error", 1);
		}
		if (0 != trk_opened(t)) {
			trk_free(t);
			r = -1;
			break;
		}

		if (t->props.print_time)
			ffps_perf(&t->psperf, FFPS_PERF_REALTIME | FFPS_PERF_CPUTIME | FFPS_PERF_RUSAGE);

		t->wflags = (cmd == FMED_TRACK_XSTART) ? FMED_WORKER_FPARALLEL : 0;
		t->wid = core->cmd(FMED_WORKER_ASSIGN, &t->kq, t->wflags);

		core->cmd(FMED_TASK_XPOST, &t->tsk, t->wid);
		break;

	case FMED_TRACK_PAUSE:
		if (trk == (void*)-1) {
			FF_ASSERT(core_ismainthr());
			FFLIST_WALKSAFE(&g->trks, t, sib, next) {
				t->state = TRK_ST_PAUSED;
			}
			break;
		}

		t->state = TRK_ST_PAUSED;
		break;

	case FMED_TRACK_UNPAUSE:
		if (trk == (void*)-1) {
			FF_ASSERT(core_ismainthr());
			FFLIST_WALKSAFE(&g->trks, t, sib, next) {
				if (t->state == TRK_ST_PAUSED) {
					t->state = TRK_ST_ACTIVE;
					trk_process(t);
				}
			}
			break;
		}

		if (t->state == TRK_ST_PAUSED) {
			t->state = TRK_ST_ACTIVE;
			trk_process(t);
		}
		break;

	case FMED_TRACK_LAST:
		g->last = 1;
		if (g->trks.len != 0)
			break;
		if (g->mon != NULL)
			g->mon->onsig(&t->props, FMED_TRK_ONLAST);
		break;

	case FMED_TRACK_WAKE:
		core->cmd(FMED_TASK_XPOST, &t->tsk, t->wid);
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
		r = g->qu->cmd2(FMED_QUE_HAVEUSERMETA, qent, 0);
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

	case FMED_TRACK_KQ:
		r = (size_t)t->kq;
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
			if (NULL == (val = g->qu->meta_find(qent, nm.ptr, nm.len)))
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
