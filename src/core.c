/** fmedia core.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/crc.h>
#include <FF/path.h>
#include <FF/filemap.h>
#include <FF/data/conf.h>
#include <FF/data/utf8.h>
#include <FF/time.h>
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
	char name_s[0];
} core_mod;

typedef struct fmed_f {
	fflist_item sib;
	void *ctx;
	fmed_filt d;
	const fmed_modinfo *mod;
	fftime clk;
	unsigned opened :1
		, noskip :1;
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

typedef struct fm_src fm_src;

enum TRK_ST {
	TRK_ST_STOPPED,
	TRK_ST_ACTIVE,
	TRK_ST_PAUSED,
	TRK_ST_ERR,
};

struct fm_src {
	fflist_item sib;
	ffchain filt_chain;
	struct {FFARR(fmed_f)} filters;
	fflist_cursor cur;
	ffrbtree dict;
	ffrbtree meta;

	ffstr id;
	char sid[FFSLEN("*") + FFINT_MAXCHARS];

	uint state; //enum TRK_ST
	uint capture :1
		, mxr_out :1;
};


static fmedia *fmed;
typedef fmedia fmed_config;

enum {
	FMED_KQ_EVS = 100,
	CONF_MBUF = 4096,
};

#ifdef FF_UNIX
#define USR_CONF  "$HOME/.config/fmedia/fmedia.conf"
#else
#define USR_CONF  "%APPDATA%/fmedia/fmedia.conf"
#endif


FF_EXP fmed_core* core_init(fmedia **ptr);
FF_EXP void core_free(void);

static int media_setout(fm_src *src);
static int media_opened(fm_src *src);
static fmed_f* newfilter(fm_src *src, const char *modname);
static fmed_f* newfilter1(fm_src *src, const fmed_modinfo *mod);
static int media_open(fm_src *src, const char *fn);
static void media_open_capt(fm_src *src);
static void media_open_mix(fm_src *src);
static void media_free(fm_src *src);
static void media_process(void *udata);
static void media_stop(fm_src *src, uint flags);
static fmed_f* media_modbyext(fm_src *src, const ffstr3 *map, const ffstr *ext);
static void media_printtime(fm_src *src);

static dict_ent* dict_add(fm_src *src, const char *name, uint *f);
static void dict_ent_free(dict_ent *e);

static void* trk_create(uint cmd, const char *url);
static int trk_cmd(void *trk, uint cmd);
static int64 trk_popval(void *trk, const char *name);
static int64 trk_getval(void *trk, const char *name);
static const char* trk_getvalstr(void *trk, const char *name);
static int trk_setval(void *trk, const char *name, int64 val);
static int trk_setvalstr(void *trk, const char *name, const char *val);
static int64 trk_setval4(void *trk, const char *name, int64 val, uint flags);
static char* trk_setvalstr4(void *trk, const char *name, const char *val, uint flags);
static char* trk_getvalstr3(void *trk, const void *name, uint flags);
static const fmed_track _fmed_track = {
	&trk_create, &trk_cmd,
	&trk_popval, &trk_getval, &trk_getvalstr, &trk_setval, &trk_setvalstr, &trk_setval4, &trk_setvalstr4, &trk_getvalstr3
};

static const fmed_mod* fmed_getmod_core(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_file(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_tui(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_sndmod(const fmed_core *_core);
extern const fmed_mod* fmed_getmod_queue(const fmed_core *_core);

static int core_open(void);
static void core_playdone(void);
static int core_sigmods(uint signo);
static const fmed_modinfo* core_getmodinfo(const ffstr *name);

static const void* core_iface(const char *name);
static int core_sig2(uint signo);
static void core_destroy(void);
static const fmed_mod fmed_core_mod = {
	&core_iface, &core_sig2, &core_destroy
};

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
static fmed_core *core = &_fmed_core;

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
static const ffpars_enumlist im_enum = { im_enumstr, FFCNT(im_enumstr), FFPARS_DSTOFF(fmedia, instance_mode) };

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
	fmed_config *conf = obj;

	if (ffstr_eqcz(val, "#tui.tui") && (conf->silent || conf->gui))
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

	if (ffstr_eqcz(name, "gui.gui") && !fmed->gui) {
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
	, { "rate",  FFPARS_TINT | FFPARS_FNOTZERO, FFPARS_DSTOFF(fmedia, inp_pcm.sample_rate) }
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
		fmed->inmap_curmod = mod;
		return 0;
	}

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffarr_grow(map, n, 64 | FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	it = (void*)(map->ptr + map->len);
	map->len += n;
	it->mod = fmed->inmap_curmod;
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
	ffstr *val = &ps->vals[0];
	const fmed_modinfo *mod;

	if (ps->p->type != FFCONF_TKEYCTX)
		return FFPARS_EUKNKEY;

	if (ffstr_eqcz(val, "core"))
		ffpars_setargs(ctx, fmed, fmed_conf_args, FFCNT(fmed_conf_args));

	else if (fmed->usrconf_modname == NULL) {
		if (NULL == (fmed->usrconf_modname = ffsz_alcopy(val->ptr, val->len)))
			return FFPARS_ESYS;
		ffpars_setargs(ctx, fmed, fmed_confusr_args, FFCNT(fmed_confusr_args));

	} else {
		ffstr3 s = {0};
		if (0 == ffstr_catfmt(&s, "%s.%S", fmed->usrconf_modname, val))
			return FFPARS_ESYS;
		mod = core_getmodinfo((ffstr*)&s);

		ffmem_free(fmed->usrconf_modname);
		fmed->usrconf_modname = NULL;
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
		ffpars_setargs(&ctx, fmed, fmed_confusr_args, FFCNT(fmed_confusr_args));
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

		ffpars_setargs(&ctx, fmed, fmed_conf_args, FFCNT(fmed_conf_args));
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
	ffstr s;
	ffstr_setz(&s, modname);
	mod = core_getmodinfo(&s);
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

	if (fmed->trackno != NULL) {
		trk_setvalstr4(src, "input_trackno", fmed->trackno, FMED_TRK_FACQUIRE);
		fmed->trackno = NULL;
	}

	if (ffs_match(fn, ffsz_len(fn), "http", 4)) {
		newfilter(src, "net.icy");
		ffstr_setz(&ext, "mp3");
		if (NULL == media_modbyext(src, &fmed->inmap, &ext))
			return 1;
	} else {
		newfilter(src, "#file.in");

		ffpath_splitname(fn, ffsz_len(fn), NULL, &ext);
		if (NULL == media_modbyext(src, &fmed->inmap, &ext))
			return 1;
	}

	newfilter(src, "#soundmod.until");

	if (fmed->mix && !src->mxr_out)
		trk_setval(src, "type", FMED_TRK_TYPE_MIXIN);

	return 0;
}

static void media_open_capt(fm_src *src)
{
	if (fmed->captdev_name != 0)
		trk_setval(src, "capture_device", fmed->captdev_name);

	trk_setval(src, "pcm_format", fmed->inp_pcm.format);

	trk_setval(src, "pcm_channels", fmed->inp_pcm.channels & FFPCM_CHMASK);
	if (fmed->inp_pcm.channels & ~FFPCM_CHMASK)
		trk_setval(src, "conv_channels", fmed->inp_pcm.channels);

	trk_setval(src, "pcm_sample_rate", fmed->inp_pcm.sample_rate);
	newfilter1(src, fmed->input);

	newfilter(src, "#soundmod.until");
	newfilter(src, "#soundmod.rtpeak");

	src->capture = 1;
	trk_setval(src, "type", FMED_TRK_TYPE_REC);
}

static void media_open_mix(fm_src *src)
{
	newfilter(src, "#queue.track");
	newfilter(src, "mixer.out");
	src->mxr_out = 1;
}

static int media_setout(fm_src *src)
{
	const char *s, *fn;
	int type = trk_getval(src, "type");

	if (type == FMED_TRK_TYPE_NETIN) {
		ffstr ext;
		const char *input = trk_getvalstr(src, "input");
		ffpath_splitname(input, ffsz_len(input), NULL, &ext);
		if (NULL == media_modbyext(src, &fmed->inmap, &ext))
			return -1;

	} else if (type != FMED_TRK_TYPE_MIXIN) {
		if (fmed->gui)
			newfilter(src, "gui.gui");
		else if (!fmed->silent)
			newfilter(src, "#tui.tui");
	}

	if (!src->mxr_out) { //apply gain to mix-in tracks only
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
	}

	newfilter(src, "#soundmod.conv");
	newfilter(src, "#soundmod.conv-soxr");

	if (fmed->preserve_date)
		trk_setval(src, "out_preserve_date", 1);

	if (type == FMED_TRK_TYPE_MIXIN) {
		newfilter(src, "mixer.in");

	} else if (fmed->pcm_peaks) {
		if (fmed->pcm_crc)
			trk_setval(src, "pcm_crc", 1);
		newfilter(src, "#soundmod.peaks");

	} else if (FMED_PNULL != (s = trk_getvalstr(src, "output"))) {
		ffstr name, ext;
		ffpath_splitname(s, ffsz_len(s), &name, &ext);
		if (NULL == media_modbyext(src, &fmed->outmap, &ext))
			return -1;
		newfilter(src, "#file.out");

	} else if (fmed->outfn.len != 0 && !fmed->rec) {
		ffstr name, ext;
		ffs_rsplit2by(fmed->outfn.ptr, fmed->outfn.len, '.', &name, &ext);

		if (name.len != 0)
			trk_setvalstr(src, "output", fmed->outfn.ptr);

		else if (FMED_PNULL != (fn = trk_getvalstr(src, "input"))) {

			ffstr fname;
			ffstr3 outfn = {0};

			ffpath_split2(fn, ffsz_len(fn), NULL, &fname);
			ffpath_splitname(fname.ptr, fname.len, &name, NULL);

			if (0 == ffstr_catfmt(&outfn, "%S/%S.%S%Z"
				, &fmed->outdir, &name, &ext))
				return -1;
			trk_setvalstr4(src, "output", outfn.ptr, FMED_TRK_FACQUIRE);

		} else {
			errlog(core, src, "core", "--out must be set");
			return -1;
		}

		if (fmed->out_copy) {
			trk_setval(src, "out-copy", 1);
			if (fmed->output != NULL)
				newfilter1(src, fmed->output);
			return 0;
		}

		if (fmed->stream_copy)
			trk_setval(src, "stream_copy", 1);

		if (NULL == media_modbyext(src, &fmed->outmap, &ext))
			return -1;

		newfilter(src, "#file.out");

	} else if (fmed->output != NULL) {
		newfilter1(src, fmed->output);
	}

	return 0;
}

static int media_opened(fm_src *src)
{
	fmed_f *f;
	FFARR_WALK(&src->filters, f) {

		if (f->mod == NULL)
			return -1;

		ffchain_add(&src->filt_chain, &f->sib);
	}

	fflist_ins(&fmed->srcs, &src->sib);
	src->state = TRK_ST_ACTIVE;
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
	uint nout = 5;
	fm_src *src = ffmem_tcalloc1(fm_src);
	if (src == NULL)
		return NULL;
	ffchain_init(&src->filt_chain);
	ffrbt_init(&src->dict);
	ffrbt_init(&src->meta);

	src->id.len = ffs_fmt(src->sid, src->sid + sizeof(src->sid), "*%u", ++fmed->srcid);
	src->id.ptr = src->sid;

	if (fmed->playdev_name != 0)
		trk_setval(src, "playdev_name", fmed->playdev_name);

	if (fmed->seek_time != 0)
		trk_setval(src, "seek_time", fmed->seek_time);

	if (fmed->overwrite)
		trk_setval(src, "overwrite", 1);

	if (fmed->out_format != 0)
		trk_setval(src, "conv_pcm_format", fmed->out_format);
	if (fmed->out_channels != 0)
		trk_setval(src, "conv_channels", fmed->out_channels);
	if (fmed->out_rate != 0)
		trk_setval(src, "conv_pcm_rate", fmed->out_rate);

	if (fmed->ogg_qual != -255)
		trk_setval(src, "ogg-quality", fmed->ogg_qual * 10);
	else if (fmed->mpeg_qual != 0xffff)
		trk_setval(src, "mpeg-quality", fmed->mpeg_qual);
	else if (fmed->aac_qual != (uint)-1)
		trk_setval(src, "aac-quality", fmed->aac_qual);
	else if (fmed->flac_complevel != 0xff)
		trk_setval(src, "flac_complevel", fmed->flac_complevel);

	if (fmed->until_time != 0) {
		trk_setval(src, "until_time", fmed->until_time);
	}

	if (fmed->rec)
		trk_setval(src, "low_latency", 1);

	switch (cmd) {
	case FMED_TRACK_OPEN:
		if (NULL == ffarr_grow(&src->filters, 6 + nout, 0))
			goto fail;
		if (0 != media_open(src, fn)) {
			media_free(src);
			return FMED_TRK_EFMT;
		}
		break;

	case FMED_TRACK_REC:
		if (NULL == ffarr_grow(&src->filters, 2 + nout, 0))
			goto fail;
		media_open_capt(src);
		break;

	case FMED_TRACK_MIX:
		if (NULL == ffarr_grow(&src->filters, 2 + nout, 0))
			goto fail;
		media_open_mix(src);
		fmed->mix = 1;
		break;

	case FMED_TRACK_NET: {
		if (NULL == ffarr_grow(&src->filters, 2 + nout, 0))
			goto fail;
		newfilter(src, "net.in");
		trk_setval(src, "type", FMED_TRK_TYPE_NETIN);
		break;
	}
	}

	if (fmed->meta.len != 0)
		trk_setvalstr(src, "meta", fmed->meta.ptr);
	return src;

fail:
	media_free(src);
	return NULL;
}

static void media_stop(fm_src *src, uint flags)
{
	fmed_f *f;
	trk_setval(src, "stopped", flags);
	FFARR_WALK(&src->filters, f) {
		f->d.flags |= FMED_FSTOP;
	}
	if (src->state == TRK_ST_STOPPED)
		media_free(src);
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
	ffstr_catfmt(&s, "time: %u.%06u.  ", all.s, all.mcs);

	FFARR_WALK(&src->filters, pf) {
		ffstr_catfmt(&s, "%s: %u.%06u (%u%%), "
			, pf->mod->name, pf->clk.s, pf->clk.mcs, fftime_mcs(&pf->clk) * 100 / fftime_mcs(&all));
	}
	if (s.len > FFSLEN(", "))
		s.len -= FFSLEN(", ");

	dbglog(core, src, "core", "%S", &s);
	ffarr_free(&s);
}

static void dict_ent_free(dict_ent *e)
{
	if (e->acq)
		ffmem_free(e->pval);
	ffmem_free(e);
}

static void media_free(fm_src *src)
{
	fmed_f *pf;
	dict_ent *e;
	fftree_node *node, *next;

	dbglog(core, src, "core", "media: closing...");
	FFARR_WALK(&src->filters, pf) {
		if (pf->ctx != NULL)
			pf->mod->f->close(pf->ctx);
	}

	if (core->loglev & FMED_LOG_DEBUG)
		media_printtime(src);

	FFTREE_WALKSAFE(&src->dict, node, next) {
		e = FF_GETPTR(dict_ent, nod, node);
		ffrbt_rm(&src->dict, &e->nod);
		dict_ent_free(e);
	}

	FFTREE_WALKSAFE(&src->meta, node, next) {
		e = FF_GETPTR(dict_ent, nod, node);
		ffrbt_rm(&src->meta, &e->nod);
		dict_ent_free(e);
	}

	if (fflist_exists(&fmed->srcs, &src->sib))
		fflist_rm(&fmed->srcs, &src->sib);

	if (src->mxr_out)
		fmed->mix = 0;

	dbglog(core, src, "core", "media: closed");
	ffmem_free(src);
}

// enum FMED_R
static const char *const fmed_retstr[] = {
	"err", "ok", "data", "more", "async", "done", "done-prev", "last-out", "fin", "syserr",
};

static void media_process(void *udata)
{
	fm_src *src = udata;
	fmed_f *nf;
	fmed_f *f;
	int r = FFLIST_CUR_NEXT, e;
	fftime t1, t2;

	if (src->cur == NULL) {
		src->cur = &src->filters.ptr->sib;
		f = FF_GETPTR(fmed_f, sib, src->cur);
		goto next;
	}

	for (;;) {

		if (src->state != TRK_ST_ACTIVE) {
			if (src->state == TRK_ST_ERR)
				goto fin;
			return;
		}

		f = FF_GETPTR(fmed_f, sib, src->cur);

#ifdef _DEBUG
		dbglog(core, src, "core", "%s calling %s, input: %L"
			, (r == FFLIST_CUR_NEXT) ? ">>" : "<<", f->mod->name, f->d.datalen);
#endif
		if (core->loglev & FMED_LOG_DEBUG) {
			ffclk_get(&t1);
		}

		e = f->mod->f->process(f->ctx, &f->d);

		if (core->loglev & FMED_LOG_DEBUG) {
			ffclk_get(&t2);
			ffclk_diff(&t1, &t2);
			fftime_add(&f->clk, &t2);
		}

#ifdef _DEBUG
		dbglog(core, src, "core", "%s returned: %s, output: %L"
			, f->mod->name, ((uint)(e + 1) < FFCNT(fmed_retstr)) ? fmed_retstr[e + 1] : "", f->d.outlen);
#endif

		switch (e) {
		case FMED_RSYSERR:
			syserrlog(core, src, f->mod->name, "%s", "system error");
			// break
		case FMED_RERR:
			src->state = TRK_ST_ERR;
			goto fin;

		case FMED_RASYNC:
			return;

		case FMED_RMORE:
			r = FFLIST_CUR_PREV;
			break;

		case FMED_ROK:
			r = FFLIST_CUR_NEXT;
			break;

		case FMED_RDATA:
			f->noskip = 1;
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
			errlog(core, src, "core", "unknown return code from module: %u", e);
			src->state = TRK_ST_ERR;
			goto fin;
		}

		if (f->d.datalen != 0)
			r |= FFLIST_CUR_SAMEIFBOUNCE;

shift:
		r = fflist_curshift(&src->cur, r | FFLIST_CUR_BOUNCE, ffchain_sentl(&src->filt_chain));

		switch (r) {
		case FFLIST_CUR_NONEXT:
			goto fin; //done

		case FFLIST_CUR_NOPREV:
			errlog(core, src, "core", "module %s requires more input data", f->mod->name);
			src->state = TRK_ST_ERR;
			goto fin;

		case FFLIST_CUR_NEXT:
next:
			nf = FF_GETPTR(fmed_f, sib, src->cur);
			nf->d.data = f->d.out;
			nf->d.datalen = f->d.outlen;
			if (src->cur->prev == ffchain_sentl(&src->filt_chain))
				nf->d.flags |= FMED_FLAST;

			if (!nf->opened) {
				if (f->d.flags & FMED_FSTOP)
					goto fin; // calling the rest of the chain is pointless

				dbglog(core, src, "core", "creating context for %s...", nf->mod->name);
				nf->ctx = nf->mod->f->open(&nf->d);
				if (nf->ctx == NULL) {
					src->state = TRK_ST_ERR;
					goto fin;

				} else if (nf->ctx == FMED_FILT_SKIP) {
					dbglog(core, src, "core", "%s is skipped", nf->mod->name);
					nf->ctx = NULL; //don't call fmed_filter.close()
					nf->d.out = f->d.out;
					nf->d.outlen = f->d.outlen;
					f = nf;
					r = FFLIST_CUR_NEXT | FFLIST_CUR_RM;
					goto shift;
				}

				dbglog(core, src, "core", "context for %s created", nf->mod->name);
				nf->opened = 1;
			}
			break;

		case FFLIST_CUR_SAME:
		case FFLIST_CUR_PREV:
			nf = FF_GETPTR(fmed_f, sib, src->cur);
			if (nf->noskip) {
				nf->noskip = 0;
			} else if (nf->d.datalen == 0 && !(nf->d.flags & FMED_FLAST)) {
				r = FFLIST_CUR_PREV;
				f = nf;
				goto shift;
			}
			break;

		default:
			FF_ASSERT(0);
		}
	}

	return;

fin:
	if (src->state == TRK_ST_ERR)
		trk_setval(src, "error", 1);

	if (!fmed->gui) {
		if (src->capture) {
			fmed->recording = 0;
			core_playdone();
		}
	}

	media_free(src);
}


static dict_ent* dict_findstr(fm_src *src, const ffstr *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_get(name->ptr, name->len);
	ffrbt_node *nod;

	nod = ffrbt_find(&src->dict, crc, NULL);
	if (nod == NULL)
		return NULL;
	ent = (dict_ent*)nod;

	if (!ffstr_eqz(name, ent->name))
		return NULL;

	return ent;
}

static dict_ent* dict_find(fm_src *src, const char *name)
{
	ffstr s;
	ffstr_setz(&s, name);
	return dict_findstr(src, &s);
}

static dict_ent* dict_add(fm_src *src, const char *name, uint *f)
{
	dict_ent *ent;
	uint crc = ffcrc32_getz(name, 0);
	ffrbt_node *nod, *parent;
	ffrbtree *tree = (*f & FMED_TRK_META) ? &src->meta : &src->dict;

	nod = ffrbt_find(tree, crc, &parent);
	if (nod != NULL) {
		ent = (dict_ent*)nod;
		if (0 != ffsz_cmp(name, ent->name)) {
			errlog(core, NULL, "core", "setval: CRC collision: %u, key: %s, with key: %s"
				, crc, name, ent->name);
			src->state = TRK_ST_ERR;
			return NULL;
		}
		*f = 1;

	} else {
		ent = ffmem_tcalloc1(dict_ent);
		if (ent == NULL) {
			errlog(core, NULL, "core", "setval: %e", FFERR_BUFALOC);
			src->state = TRK_ST_ERR;
			return NULL;
		}
		ent->nod.key = crc;
		ffrbt_insert(tree, &ent->nod, parent);
		ent->name = name;
		*f = 0;
	}

	return ent;
}

static dict_ent* meta_find(fm_src *src, const ffstr *name)
{
	dict_ent *ent;
	uint crc = ffcrc32_get(name->ptr, name->len);
	ffrbt_node *nod;

	nod = ffrbt_find(&src->meta, crc, NULL);
	if (nod == NULL)
		return NULL;
	ent = (dict_ent*)nod;

	if (!ffstr_eqz(name, ent->name))
		return NULL;

	return ent;
}


static int trk_cmd(void *trk, uint cmd)
{
	fm_src *src = trk;
	fflist_item *next;

	dbglog(core, NULL, "track", "received command:%u, trk:%p", cmd, trk);

	switch (cmd) {
	case FMED_TRACK_STOPALL_EXIT:
		if (fmed->srcs.len == 0 || fmed->stopped) {
			core_sig(FMED_STOP);
			break;
		}
		fmed->stopped = 1;
		trk = (void*)-1;
		// break

	case FMED_TRACK_STOPALL:
		FFLIST_WALKSAFE(&fmed->srcs, src, sib, next) {
			if (src->capture && trk == NULL)
				continue;

			media_stop(src, cmd);
		}
		break;

	case FMED_TRACK_STOP:
		media_stop(src, FMED_TRACK_STOP);
		break;

	case FMED_TRACK_START:
		if (0 != media_setout(src)) {
			trk_setval(src, "error", 1);
		}
		if (0 != media_opened(src)) {
			media_free(src);
			return -1;
		}

		media_process(src);
		break;

	case FMED_TRACK_PAUSE:
		src->state = TRK_ST_PAUSED;
		break;
	case FMED_TRACK_UNPAUSE:
		src->state = TRK_ST_ACTIVE;
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
		dict_ent_free(ent);
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

static char* trk_getvalstr3(void *trk, const void *name, uint flags)
{
	fm_src *src = trk;
	dict_ent *ent;
	ffstr nm;

	if (flags & FMED_TRK_NAMESTR)
		ffstr_set2(&nm, (ffstr*)name);
	else
		ffstr_setz(&nm, (char*)name);

	if (flags & FMED_TRK_META) {
		ent = meta_find(src, &nm);
		if (ent == NULL) {
			void *qent;
			if (NULL == (qent = (void*)trk_getval(src, "queue_item")))
				return FMED_PNULL;
			const fmed_queue *qu = core->getmod("#queue.queue");
			ffstr *val;
			if (NULL == (val = qu->meta_find(qent, nm.ptr, nm.len)))
				return FMED_PNULL;
			return val->ptr;
		}
	} else
		ent = dict_findstr(src, &nm);
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
	fm_src *src = trk;
	uint st = 0;
	dict_ent *ent = dict_add(src, name, &st);
	if (ent == NULL)
		return FMED_NULL;

	if ((flags & FMED_TRK_FNO_OVWRITE) && st == 1)
		return ent->val;

	if (ent->acq) {
		ffmem_free(ent->pval);
		ent->acq = 0;
	}

	ent->val = val;
	dbglog(core, trk, "core", "setval: %s = %D", name, val);
	return val;
}

static char* trk_setvalstr4(void *trk, const char *name, const char *val, uint flags)
{
	fm_src *src = trk;
	dict_ent *ent;
	uint st = flags;

	if (flags & FMED_TRK_META) {
		ent = dict_add(src, name, &st);
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
		dbglog(core, trk, "core", "set meta: %s = %s", name, ent->pval);
		return ent->pval;

	} else
		ent = dict_add(src, name, &st);

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

	dbglog(core, trk, "core", "setval: %s = %s", name, val);
	return ent->pval;
}

static int trk_setvalstr(void *trk, const char *name, const char *val)
{
	trk_setvalstr4(trk, name, val, 0);
	return 0;
}


static void core_playdone(void)
{
	if (!fmed->recording) {
		fmed->stopped = 1;
		ffkqu_post(fmed->kq, &fmed->evposted, NULL);
	}
}

static void core_posted(void *udata)
{
}


fmed_core* core_init(fmedia **ptr)
{
	ffmem_init();
	fflk_setup();
	fmed = ffmem_tcalloc1(fmedia);
	if (fmed == NULL)
		return NULL;
	fmed->log = &log_dummy;

	fmed->volume = 100;
	fmed->kq = FF_BADFD;
	fftask_init(&fmed->taskmgr);
	fflist_init(&fmed->srcs);
	fflist_init(&fmed->mods);
	core_insmod("#core.core", NULL);

	fmed->ogg_qual = -255;
	fmed->aac_qual = (uint)-1;
	fmed->mpeg_qual = 0xffff;
	fmed->flac_complevel = 0xff;
	fmed->cue_gaps = 255;
	fmed->codepage = FFU_WIN1252;
	if (NULL == ffstr_copy(&fmed->outdir, FFSTR("."))) {
		core_free();
		return NULL;
	}

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

	ffstr_free(&fmed->meta);
	ffmem_safefree(fmed->trackno);

	ffarr_free(&fmed->inmap);
	ffarr_free(&fmed->outmap);

	FFLIST_WALKSAFE(&fmed->mods, mod, sib, next) {
		mod->m->destroy();
		if (mod->dl != NULL)
			ffdl_close(mod->dl);
		ffmem_free(mod);
	}

	if (fmed->kq != FF_BADFD) {
		ffkqu_post_attach(FF_BADFD);
		ffkqu_close(fmed->kq);
	}

	ffstr_free(&fmed->root);
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

		dbglog(core, NULL, "core", "loading module %s from %s", sname, fn);

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

	mod->f = mod->m->iface(modname.ptr);
	if (mod->f == NULL)
		goto fail;

	mod->dl = dl;
	ffsz_fcopy(mod->name_s, sname, sname_len);
	mod->name = mod->name_s;
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

static int core_open(void)
{
	if (FF_BADFD == (fmed->kq = ffkqu_create())) {
		syserrlog(core, NULL, "core", "%e", FFERR_KQUCREAT);
		return 1;
	}
	core->kq = fmed->kq;
	ffkqu_post_attach(fmed->kq);

	fmed->pkqutime = ffkqu_settm(&fmed->kqutime, (uint)-1);

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
		core->loglev = fmed->debug ? FMED_LOG_DEBUG : 0;
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
		return fmed->repeat_all;
	else if (!ffsz_cmp(name, "codepage"))
		return fmed->codepage;
	else if (!ffsz_cmp(name, "gui"))
		return fmed->gui;
	else if (!ffsz_cmp(name, "next_if_error"))
		return fmed->silent;
	else if (!ffsz_cmp(name, "show_tags"))
		return fmed->tags;
	else if (!ffsz_cmp(name, "cue_gaps") && fmed->cue_gaps != 255)
		return fmed->cue_gaps;
	else if (!ffsz_cmp(name, "instance_mode"))
		return fmed->instance_mode;
	return FMED_NULL;
}

static const char *const loglevs[] = {
	"debug", "info", "warning", "error",
};

static void core_log(uint flags, void *trk, const char *module, const char *fmt, ...)
{
	fm_src *src = trk;
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

	if (module == NULL && src != NULL && src->cur != NULL) {
		const fmed_f *f = FF_GETPTR(fmed_f, sib, src->cur);
		module = f->mod->name;
	}
	ld.module = module;

	if (flags & FMED_LOG_SYS)
		fferr_set(e);

	ld.ctx = (src != NULL) ? &src->id : NULL;
	ld.fmt = fmt;
	va_start(ld.va, fmt);
	fmed->log->log(flags, &ld);
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
