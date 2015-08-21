/** fmedia core.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/crc.h>
#include <FF/path.h>
#include <FF/filemap.h>
#include <FF/data/conf.h>
#include <FFOS/error.h>
#include <FFOS/process.h>
#include <FFOS/timer.h>


typedef struct core_mod {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const fmed_filter *f;

	ffstr mname;
	fflist_item sib;
} core_mod;

typedef struct fmed_f {
	fflist_item sib;
	void *ctx;
	fmed_filt d;
	const fmed_modinfo *mod;
	fftime clk;
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

typedef struct fm_src fm_src;

struct fm_src {
	fflist_item sib;
	struct {FFARR(fmed_f)} filters;
	fflist_cursor cur;
	ffrbtree dict;

	ffstr id;
	char sid[FFSLEN("*") + FFINT_MAXCHARS];

	char *outfn;

	unsigned err :1
		, capture :1;
};


static fmedia *fmed;
typedef fmedia fmed_config;

enum {
	FMED_KQ_EVS = 100
};


FF_EXP fmed_core* core_init(fmedia **ptr, fmed_log_t logfunc);
FF_EXP void core_free(void);

static int media_setout(fm_src *src, const char *fn);
static fmed_f* newfilter(fm_src *src, const char *modname);
static fmed_f* newfilter1(fm_src *src, const fmed_modinfo *mod);
static int media_open(fm_src *src, const char *fn);
static void media_open_capt(fm_src *src);
static void media_open_mix(fm_src *src);
static void media_free(fm_src *src);
static void media_process(void *udata);
static fmed_f* media_modbyext(fm_src *src, const ffstr3 *map, const ffstr *ext);
static void media_printtime(fm_src *src);

static void* trk_create(uint cmd, const char *url);
static int trk_cmd(void *trk, uint cmd);
static int64 trk_popval(void *trk, const char *name);
static int64 trk_getval(void *trk, const char *name);
static const char* trk_getvalstr(void *trk, const char *name);
static int trk_setval(void *trk, const char *name, int64 val);
static int trk_setvalstr(void *trk, const char *name, const char *val);
static const fmed_track _fmed_track = {
	&trk_create, &trk_cmd,
	&trk_popval, &trk_getval, &trk_getvalstr, &trk_setval, &trk_setvalstr
};

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_tui(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);

static int core_open(void);
static void core_playdone(void);
static int core_sigmods(uint signo);
static const fmed_modinfo* core_getmodinfo(const char *name);

static const void* core_iface(const char *name);
static int core_sig2(uint signo);
static void core_destroy(void);
static const fmed_mod fmed_core_mod = {
	&core_iface, &core_sig2, &core_destroy
};

static int64 core_getval(const char *name);
static void core_log(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...);
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
static fmed_core *core = &_fmed_core;

static int fmed_conf(void);
static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_setmod(const fmed_modinfo **pmod, ffstr *val);
static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_input(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_ext(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_ext_val(ffparser_schem *p, void *obj, ffstr *val);

static const ffpars_arg fmed_conf_args[] = {
	{ "mod",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_mod) }
	, { "mod_conf",  FFPARS_TOBJ | FFPARS_FOBJ1 | FFPARS_FNOTEMPTY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_modconf) }
	, { "output",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_output) }
	, { "input",  FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&fmed_conf_input) }
	, { "input_ext",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_ext) }
	, { "output_ext",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_ext) }
};


static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	if (!(ffstr_eqcz(val, "gui.gui") && !conf->gui)
		&& NULL == core->insmod(val->ptr, NULL))
		return FFPARS_ESYS;
	ffstr_free(val);
	return 0;
}

static int fmed_conf_modconf(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	const ffstr *name = &p->vals[0];
	char *zname = ffsz_alcopy(name->ptr, name->len);
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
	char s[4096];
	ffsz_copy(s, sizeof(s), val->ptr, val->len);
	if (NULL == (*pmod = core_getmodinfo(s))) {
		return FFPARS_EBADVAL;
	}
	return 0;
}

static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	return fmed_conf_setmod(&conf->output, val);
}

static int pcm_formatstr(const char *s, size_t len)
{
	if (ffs_eqcz(s, len, "16le"))
		return FFPCM_16LE;
	else if (ffs_eqcz(s, len, "32le"))
		return FFPCM_32LE;
	else if (ffs_eqcz(s, len, "float"))
		return FFPCM_FLOAT;
	return -1;
}

static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val)
{
	fmed_config *conf = obj;
	if (-1 == (conf->inp_pcm.format = pcm_formatstr(val->ptr, val->len)))
		return FFPARS_EBADVAL;
	return 0;
}

static const ffpars_arg fmed_conf_input_args[] = {
	{ "format",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_inp_format) }
	, { "channels",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(fmedia, inp_pcm.channels) }
	, { "rate",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(fmedia, inp_pcm.sample_rate) }
};

static int fmed_conf_input(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	fmed_config *conf = obj;
	int r;
	if (0 != (r = fmed_conf_setmod(&conf->input, &p->vals[0])))
		return r;
	ffpars_setargs(ctx, conf, fmed_conf_input_args, FFCNT(fmed_conf_input_args));
	return 0;
}

static int fmed_conf_ext_val(ffparser_schem *p, void *obj, ffstr *val)
{
	size_t n;
	inmap_item *it;
	ffstr3 *map = obj;

	if (NULL != ffs_findc(val->ptr, val->len, '.')) {
		const fmed_modinfo *mod = core_getmodinfo(val->ptr);
		if (mod == NULL) {
			return FFPARS_EBADVAL;
		}
		ffstr_free(val);
		fmed->inmap_curmod = mod;
		return 0;
	}

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffarr_grow(map, n, FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	it = (void*)(map->ptr + map->len);
	map->len += n;
	it->mod = fmed->inmap_curmod;
	ffmemcpy(it->ext, val->ptr, val->len + 1);
	ffstr_free(val);
	return 0;
}

static const ffpars_arg fmed_conf_ext_args[] = {
	{ "*",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FLIST, FFPARS_DST(&fmed_conf_ext_val) }
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

static int fmed_conf(void)
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

	ffpars_setargs(&ctx, fmed, fmed_conf_args, FFCNT(fmed_conf_args));
	ffconf_scheminit(&ps, &pconf, &ctx);

	if (NULL == (filename = core->getpath(FFSTR("fmedia.conf"))))
		return -1;

	if (FF_BADFD == (f = fffile_open(filename, O_RDONLY))) {
		goto err;
	}

	if (NULL == (buf = ffmem_alloc(4096))) {
		goto err;
	}

	for (;;) {
		n = fffile_read(f, buf, 4096);
		if (n == (size_t)-1) {
			goto err;
		} else if (n == 0)
			break;
		ffstr_set(&s, buf, n);

		while (s.len != 0) {
			n = s.len;
			r = ffconf_parse(&pconf, s.ptr, &n);
			ffstr_shift(&s, n);
			r = ffpars_schemrun(&ps, r);

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
	const fmed_modinfo *mod;
	mod = core_getmodinfo(modname);
	return newfilter1(src, mod);
}

static fmed_f* media_modbyext(fm_src *src, const ffstr3 *map, const ffstr *ext)
{
	const inmap_item *it = (void*)map->ptr;
	while (it != (void*)(map->ptr + map->len)) {
		size_t len = ffsz_len(it->ext);
		if (ffstr_ieq(ext, it->ext, len)) {
			return newfilter1(src, it->mod);
		}
		it = (void*)((char*)it + sizeof(inmap_item) + len + 1);
	}

	errlog(core, NULL, "core", "unknown file format: %S", ext);
	return NULL;
}

static int media_open(fm_src *src, const char *fn)
{
	ffstr ext;
	fffileinfo fi;

	trk_setvalstr(src, "input", fn);
	newfilter(src, "#queue.track");

	if (0 == fffile_infofn(fn, &fi) && fffile_isdir(fffile_infoattr(&fi))) {
		newfilter(src, "plist.dir");
		return 0;
	}

	if (fmed->info)
		trk_setval(src, "input_info", 1);

	if (fmed->fseek)
		trk_setval(src, "input_seek", fmed->fseek);

	if (fmed->trackno != 0) {
		trk_setval(src, "input_trackno", fmed->trackno);
		fmed->trackno = 0;
	}

	newfilter(src, "#file.in");

	ffpath_splitname(fn, ffsz_len(fn), NULL, &ext);
	if (NULL == media_modbyext(src, &fmed->inmap, &ext))
		return 1;

	newfilter(src, "#soundmod.until");

	if (fmed->mix) {
		newfilter(src, "mixer.in");
		return 0;
	}

	if (fmed->gui)
		newfilter(src, "gui.gui");
	else if (!fmed->silent)
		newfilter(src, "#tui.tui");

	return 0;
}

static void media_open_capt(fm_src *src)
{
	if (fmed->captdev_name != 0)
		trk_setval(src, "capture_device", fmed->captdev_name);

	trk_setval(src, "pcm_format", fmed->inp_pcm.format);
	trk_setval(src, "pcm_channels", fmed->inp_pcm.channels);
	trk_setval(src, "pcm_sample_rate", fmed->inp_pcm.sample_rate);
	newfilter1(src, fmed->input);

	src->capture = 1;
}

static void media_open_mix(fm_src *src)
{
	newfilter(src, "mixer.out");
}

static int media_setout(fm_src *src, const char *fn)
{
	if (fmed->volume != 100) {
		double db;
		if (fmed->volume < 100)
			db = ffpcm_vol2db(fmed->volume, 48);
		else
			db = ffpcm_vol2db_inc(fmed->volume - 100, 25, 6);
		trk_setval(src, "gain", db * 100);
	}
	if (fmed->gain != 0) {
		trk_setval(src, "gain", fmed->gain * 100);
	}
	newfilter(src, "#soundmod.gain");

	trk_setval(src, "conv_pcm_format", fmed->conv_pcm_formt);
	newfilter(src, "#soundmod.conv");

	if (fmed->outfn.len != 0 && (!fmed->rec || src->capture)) {
		ffstr name, ext;
		ffpath_splitname(fmed->outfn.ptr, fmed->outfn.len, &name, &ext);
		if (NULL == media_modbyext(src, &fmed->outmap, &ext))
			return -1;

		if (name.len != 0)
			trk_setvalstr(src, "output", fmed->outfn.ptr);

		else if (fmed->outdir.len != 0 && fn != NULL) {
			ffstr fname;
			ffstr3 outfn = {0};

			ffpath_split2(fn, ffsz_len(fn), NULL, &fname);
			ffpath_splitname(fname.ptr, fname.len, &name, NULL);

			if (0 == ffstr_catfmt(&outfn, "%S/%S.%S%Z"
				, &fmed->outdir, &name, &ext))
				return -1;
			src->outfn = outfn.ptr;
			trk_setvalstr(src, "output", src->outfn);

		} else {
			errlog(core, src, "core", "--out or --outdir must be set");
			return -1;
		}

		newfilter(src, "#file.out");

	} else if (fmed->output != NULL) {
		newfilter1(src, fmed->output);
	}

	return 0;
}

static void* trk_create(uint cmd, const char *fn)
{
	fmed_f *f, *prev = NULL;
	uint nout = 4;
	fm_src *src = ffmem_tcalloc1(fm_src);
	if (src == NULL)
		return NULL;
	ffrbt_init(&src->dict);

	src->id.len = ffs_fmt(src->sid, src->sid + sizeof(src->sid), "*%u", ++fmed->srcid);
	src->id.ptr = src->sid;

	if (fmed->playdev_name != 0)
		trk_setval(src, "playdev_name", fmed->playdev_name);
	trk_setval(src, "pcm_ileaved", 1);

	if (fmed->seek_time != 0)
		trk_setval(src, "seek_time", fmed->seek_time);

	if (fmed->overwrite)
		trk_setval(src, "overwrite", 1);

	if (fmed->ogg_qual != -255)
		trk_setval(src, "ogg-quality", fmed->ogg_qual * 10);

	if (fmed->until_time != 0) {
		if (fmed->until_time <= fmed->seek_time) {
			errlog(core, NULL, "core", "until_time must be bigger than seek_time");
			goto fail;
		}
		trk_setval(src, "until_time", fmed->until_time);
	}

	if (fmed->rec)
		trk_setval(src, "low_latency", 1);

	switch (cmd) {
	case FMED_TRACK_OPEN:
		if (NULL == ffarr_grow(&src->filters, 6 + nout, 0))
			goto fail;
		if (0 != media_open(src, fn))
			goto fail;
		break;

	case FMED_TRACK_REC:
		if (NULL == ffarr_grow(&src->filters, 1 + nout, 0))
			goto fail;
		media_open_capt(src);
		break;

	case FMED_TRACK_MIX:
		if (NULL == ffarr_grow(&src->filters, 1 + nout, 0))
			goto fail;
		media_open_mix(src);
		break;
	}

	if (0 != media_setout(src, fn))
		goto fail;

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

static void media_printtime(fm_src *src)
{
	fmed_f *pf;
	ffstr3 s = {0};
	fftime all = {0};

	FFARR_WALK(&src->filters, pf) {
		fftime_add(&all, &pf->clk);
	}
	if (all.s == 0 && all.mcs == 0)
		return;
	ffstr_catfmt(&s, "cpu time: %u.%06u.  ", all.s, all.mcs);

	FFARR_WALK(&src->filters, pf) {
		ffstr_catfmt(&s, "%s: %u.%06u (%u%%), "
			, pf->mod->name, pf->clk.s, pf->clk.mcs, fftime_mcs(&pf->clk) * 100 / fftime_mcs(&all));
	}
	if (s.len > FFSLEN(", "))
		s.len -= FFSLEN(", ");

	dbglog(core, src, "core", "%S", &s);
	ffarr_free(&s);
}

static void media_free(fm_src *src)
{
	fmed_f *pf;

	dbglog(core, src, "core", "media: closing...");
	FFARR_WALK(&src->filters, pf) {
		if (pf->ctx != NULL)
			pf->mod->f->close(pf->ctx);
	}

	if (core->loglev & FMED_LOG_DEBUG)
		media_printtime(src);

	ffarr_free(&src->dict);
	if (fflist_exists(&fmed->srcs, &src->sib))
		fflist_rm(&fmed->srcs, &src->sib);
	FF_SAFECLOSE(src->outfn, NULL, ffmem_free);

	dbglog(core, src, "core", "media: closed");
	ffmem_free(src);
}

static void media_process(void *udata)
{
	fm_src *src = udata;
	fmed_f *nf;
	fmed_f *f;
	int r, e, skip = 0;
	fftime t1, t2;

	for (;;) {
		if (src->err)
			goto fin;

		f = FF_GETPTR(fmed_f, sib, src->cur);
		if (!skip || (f->d.flags & FMED_FLAST)) {

			// dbglog(core, src, "core", "calling %s, input: %L"
			// 	, f->mod->name, f->d.datalen);
			if (core->loglev & FMED_LOG_DEBUG) {
				ffclk_get(&t1);
			}

			e = f->mod->f->process(f->ctx, &f->d);

			if (core->loglev & FMED_LOG_DEBUG) {
				ffclk_get(&t2);
				ffclk_diff(&t1, &t2);
				fftime_add(&f->clk, &t2);
			}

			// dbglog(core, src, "core", "%s returned: %d, output: %L"
			// 	, f->mod->name, e, f->d.outlen);

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

		if (skip)
			skip = 0;

		if (f->d.datalen != 0)
			r |= FFLIST_CUR_SAMEIFBOUNCE;
		r = fflist_curshift(&src->cur, r | FFLIST_CUR_BOUNCE, fflist_sentl(&src->cur));

		switch (r) {
		case FFLIST_CUR_NONEXT:
			goto fin; //done

		case FFLIST_CUR_NOPREV:
			errlog(core, src, "core", "module %s requires more input data", f->mod->name);
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
			nf = FF_GETPTR(fmed_f, sib, src->cur);
			if (nf->d.datalen == 0)
				skip = 1;
			break;

		default:
			FF_ASSERT(0);
		}
	}

	return;

fin:
	if (fmed->mix) {
		if (fmed->srcs.len == 0)
			core_playdone();
	} else if (src->capture) {
		fmed->recording = 0;
		core_playdone();
	}
	media_free(src);
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


static int trk_cmd(void *trk, uint cmd)
{
	fm_src *src = trk;
	fflist_item *next;
	switch (cmd) {
	case FMED_TRACK_STOPALL:
		core_sigmods(FMED_TRKSTOP);
		FFLIST_WALKSAFE(&fmed->srcs, src, sib, next) {
			media_free(src);
		}
		break;

	case FMED_TRACK_START:
		media_process(src);
		break;
	}
	return 0;
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

static void core_posted(void *udata)
{
}


fmed_core* core_init(fmedia **ptr, fmed_log_t logfunc)
{
	ffmem_init();
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return NULL;
	fmed->logfunc = logfunc;

	fmed->volume = 100;
	fmed->kq = FF_BADFD;
	fftask_init(&fmed->taskmgr);
	fflist_init(&fmed->srcs);
	core_insmod("#core.core", NULL);

	fmed->ogg_qual = -255;
	fmed->conv_pcm_formt = FFPCM_16LE;

	*ptr = fmed;
	return core;
}

void core_free(void)
{
	uint i;
	fflist_item *next;
	char **fn;
	core_mod *mod;

	trk_cmd(NULL, FMED_TRACK_STOPALL);

	fn = (char**)fmed->in_files.ptr;
	for (i = 0;  i < fmed->in_files.len;  i++, fn++) {
		ffmem_free(*fn);
	}
	ffarr_free(&fmed->in_files);
	ffstr_free(&fmed->outfn);
	ffstr_free(&fmed->outdir);

	ffarr_free(&fmed->inmap);
	ffarr_free(&fmed->outmap);

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		mod->m->destroy();
		if (mod->dl != NULL)
			ffdl_close(mod->dl);
		ffmem_free(mod->name);
		ffmem_free(mod);
	}

	if (fmed->kq != FF_BADFD)
		ffkqu_close(fmed->kq);

	ffstr_free(&fmed->root);
	ffmem_free(fmed);
	fmed = NULL;
}

static const fmed_modinfo* core_insmod(const char *sname, ffpars_ctx *ctx)
{
	const char *modname, *dot;
	fmed_getmod_t getmod;
	ffdl dl = NULL;
	ffstr s;
	core_mod *mod = ffmem_tcalloc1(core_mod);
	if (mod == NULL)
		return NULL;

	ffstr_setz(&s, sname);
	dot = ffs_findc(s.ptr, s.len, '.');
	if (dot == NULL || dot == s.ptr || dot + 1 == s.ptr + s.len) {
		fferr_set(EINVAL);
		goto fail;
	}
	modname = dot + 1;
	s.len = dot - s.ptr;

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
	ffstr_set(&mod->mname, mod->name, s.len);
	fflist_ins(&fmed->mods, &mod->sib);

	if (ctx != NULL)
		mod->f->conf(ctx);

	return (fmed_modinfo*)mod;

fail:
	if (dl != NULL)
		ffdl_close(dl);
	ffmem_free(mod);
	return NULL;
}

static const void* core_getmod(const char *name)
{
	core_mod *mod;
	ffstr smod, iface;
	ffs_split2by(name, ffsz_len(name), '.', &smod, &iface);

	FFLIST_WALK(&fmed->mods, mod, sib) {
		if (!ffstr_eq2(&smod, &mod->mname))
			continue;

		if (iface.len != 0)
			return mod->m->iface(iface.ptr);

		return (fmed_modinfo*)mod;
	}

	errlog(core, NULL, "core", "module not found: %s", name);
	return NULL;
}

static const fmed_modinfo* core_getmodinfo(const char *name)
{
	core_mod *mod;
	FFLIST_WALK(&fmed->mods, mod, sib) {
		if (!ffsz_cmp(mod->name, name))
			return (fmed_modinfo*)mod;
	}

	errlog(core, NULL, "core", "module not found: %s", name);
	return NULL;
}

static int core_open(void)
{
	if (FF_BADFD == (fmed->kq = ffkqu_create())) {
		syserrlog(core, NULL, "core", "%e", FFERR_KQUCREAT);
		return 1;
	}
	core->loglev = fmed->debug ? FMED_LOG_DEBUG : 0;
	core->kq = fmed->kq;

	fmed->pkqutime = ffkqu_settm(&fmed->kqutime, (uint)-1);

	if (fmed->srcs.len != 0) {
		fmed->playing = 1;
	}

	if (fmed->rec) {
		fmed->recording = 1;
	}

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

static int core_sigmods(uint signo)
{
	core_mod *mod;
	FFLIST_WALK(&fmed->mods, mod, sib) {
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
		if (0 != fmed_conf())
			return 1;
		break;

	case FMED_OPEN:
		if (0 != core_open())
			return 1;
		core_sigmods(signo);
		break;

	case FMED_START:
		core_work();
		return 0;

	case FMED_STOP:
		core_sigmods(signo);
		core_playdone();
		break;

	case FMED_LISTDEV:
		core_sigmods(signo);
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
		if (fmed->taskmgr.tasks.len == 1)
			ffkqu_post(fmed->kq, &fmed->evposted, NULL);
	}
}

static int64 core_getval(const char *name)
{
	if (!ffsz_cmp(name, "repeat_all"))
		return fmed->repeat_all;
	else if (!ffsz_cmp(name, "gui"))
		return fmed->gui;
	return FMED_NULL;
}

static void core_log(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...)
{
	fm_src *src = trk;
	va_list va;
	va_start(va, fmt);
	fmed->logfunc(fd, NULL, module, level, (src != NULL) ? &src->id : NULL, fmt, va);
	va_end(va);
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
