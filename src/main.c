/** fmedia config.
Copyright (c) 2015 Simon Zolin */

#include <core.h>

#include <FF/audio/pcm.h>
#include <FF/data/conf.h>
#include <FF/data/psarg.h>
#include <FF/data/utf8.h>
#include <FF/filemap.h>
#include <FF/time.h>
#include <FF/path.h>
#include <FFOS/sig.h>
#include <FFOS/error.h>
#include <FFOS/process.h>


static int fmed_cmdline(int argc, char **argv);
static int fmed_arg_usage(void);
static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val);
static int fmed_arg_listdev(void);
static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val);

static int fmed_conf(void);
static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_setmod(const fmed_modinfo **pmod, ffstr *val);
static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_mixer(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_input(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val);
static int fmed_conf_inputmap(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
static int fmed_conf_inmap_val(ffparser_schem *p, void *obj, ffstr *val);


static const ffpars_arg fmed_cmdline_args[] = {
	{ "",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FMULTI,  FFPARS_DST(&fmed_arg_infile) }

	, { "mix",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, mix) }
	, { "repeat-all",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, repeat_all) }
	, { "seek",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }
	, { "fseek",  FFPARS_TINT | FFPARS_F64BIT,  FFPARS_DSTOFF(fmedia, fseek) }
	, { "until",  FFPARS_TSTR | FFPARS_FNOTEMPTY,  FFPARS_DST(&fmed_arg_seek) }

	, { "info",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, info) }

	, { "out",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, outfn) }

	, { "record",  FFPARS_TSTR | FFPARS_FCOPY | FFPARS_FNOTEMPTY | FFPARS_FSTRZ,  FFPARS_DSTOFF(fmedia, rec_file) }

	, { "list-dev",  FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_listdev) }
	, { "dev",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, playdev_name) }
	, { "dev-capture",  FFPARS_TINT,  FFPARS_DSTOFF(fmedia, captdev_name) }

	, { "silent",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, silent) }
	, { "debug",  FFPARS_TBOOL | FFPARS_F8BIT | FFPARS_FALONE,  FFPARS_DSTOFF(fmedia, debug) }
	, { "help",  FFPARS_SETVAL('h') | FFPARS_TBOOL | FFPARS_FALONE,  FFPARS_DST(&fmed_arg_usage) }
};

static const ffpars_arg fmed_conf_args[] = {
	{ "mod",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_mod) }
	, { "output",  FFPARS_TSTR | FFPARS_FNOTEMPTY, FFPARS_DST(&fmed_conf_output) }
	, { "input",  FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&fmed_conf_input) }
	, { "input_map",  FFPARS_TOBJ, FFPARS_DST(&fmed_conf_inputmap) }
	, { "mixer",  FFPARS_TOBJ | FFPARS_FOBJ1, FFPARS_DST(&fmed_conf_mixer) }
};


static int fmed_arg_usage(void)
{
	char buf[4096];
	ssize_t n;
	char *fn;
	fffd f;

	if (NULL == (fn = core->getpath(FFSTR("help.txt"))))
		return FFPARS_ESYS;

	f = fffile_open(fn, O_RDONLY | O_NOATIME);
	ffmem_free(fn);
	if (f == FF_BADFD)
		return FFPARS_ELAST;
	n = fffile_read(f, buf, sizeof(buf));
	fffile_close(f);
	if (n > 0)
		fffile_write(ffstdout, buf, n);
	return FFPARS_ELAST;
}

static int fmed_arg_infile(ffparser_schem *p, void *obj, const ffstr *val)
{
	char **fn;
	if (NULL == _ffarr_grow(&fmed->in_files, 1, 0, sizeof(char*)))
		return FFPARS_ESYS;
	fn = ffarr_push(&fmed->in_files, char*);
	*fn = val->ptr;
	return 0;
}

static int fmed_arg_listdev(void)
{
	core->sig(FMED_LISTDEV);
	return FFPARS_ELAST;
}

static int fmed_arg_seek(ffparser_schem *p, void *obj, const ffstr *val)
{
	uint m, s, i;
	if (val->len != ffs_fmatch(val->ptr, val->len, "%2u:%2u", &m, &s))
		return FFPARS_EBADVAL;

	i = m * 60 * 1000 + s * 1000;

	if (!ffsz_cmp(p->curarg->name, "seek"))
		fmed->seek_time = i;
	else
		fmed->until_time = i;
	return 0;
}

static int fmed_cmdline(int argc, char **argv)
{
	ffparser_schem ps;
	ffparser p;
	ffpars_ctx ctx = {0};
	int r = 0, i;
	int ret = 1;

	ffpars_setargs(&ctx, fmed, fmed_cmdline_args, FFCNT(fmed_cmdline_args));
	if (0 != ffpsarg_scheminit(&ps, &p, &ctx)) {
		errlog(core, NULL, "core", "cmd line parser", NULL);
		return 1;
	}

	for (i = 1;  i < argc; ) {
		int n = 0;
		r = ffpsarg_parse(&p, argv[i], &n);
		i += n;

		r = ffpars_schemrun(&ps, r);

		if (r == FFPARS_ELAST)
			goto fail;

		if (ffpars_iserr(r))
			break;
	}

	if (!ffpars_iserr(r))
		r = ffpsarg_schemfin(&ps);

	if (ffpars_iserr(r)) {
		errlog(core, NULL, "core", "cmd line parser: %s", ffpars_errstr(r));
		goto fail;
	}

	ret = 0;

fail:
	ffpars_schemfree(&ps);
	ffpars_free(&p);
	return ret;
}


static int fmed_conf_mod(ffparser_schem *p, void *obj, ffstr *val)
{
	if (NULL == core->insmod(val->ptr))
		return FFPARS_ESYS;
	ffstr_free(val);
	return 0;
}

static int fmed_conf_setmod(const fmed_modinfo **pmod, ffstr *val)
{
	char s[4096];
	ffsz_copy(s, sizeof(s), val->ptr, val->len);
	if (NULL == (*pmod = core->getmod(s))) {
		errlog(core, NULL, "core", "module %S is not configured", val);
		return FFPARS_EBADVAL;
	}
	return 0;
}

static int fmed_conf_output(ffparser_schem *p, void *obj, ffstr *val)
{
	return fmed_conf_setmod(&fmed->output, val);
}


static int fmed_conf_inp_format(ffparser_schem *p, void *obj, ffstr *val)
{
	if (ffstr_eqcz(val, "16le"))
		fmed->inp_pcm.format = FFPCM_16LE;
	else if (ffstr_eqcz(val, "32le"))
		fmed->inp_pcm.format = FFPCM_32LE;
	else if (ffstr_eqcz(val, "float"))
		fmed->inp_pcm.format = FFPCM_FLOAT;
	else
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
	int r;
	if (0 != (r = fmed_conf_setmod(&fmed->input, &p->vals[0])))
		return r;
	ffpars_setargs(ctx, fmed, fmed_conf_input_args, FFCNT(fmed_conf_input_args));
	return 0;
}

extern int mix_conf(ffpars_ctx *ctx);

static int fmed_conf_mixer(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	return mix_conf(ctx);
}

static int fmed_conf_inmap_val(ffparser_schem *p, void *obj, ffstr *val)
{
	size_t n;
	inmap_item *it;

	if (NULL != ffs_findc(val->ptr, val->len, '.')) {
		const fmed_modinfo *mod = core->getmod(val->ptr);
		if (mod == NULL) {
			errlog(core, NULL, "core", "module %S is not configured", val);
			return FFPARS_EBADVAL;
		}
		ffstr_free(val);
		fmed->inmap_curmod = mod;
		return 0;
	}

	n = sizeof(inmap_item) + val->len + 1;
	if (NULL == ffarr_grow(&fmed->inmap, n, FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	it = (void*)(fmed->inmap.ptr + fmed->inmap.len);
	fmed->inmap.len += n;
	it->mod = fmed->inmap_curmod;
	ffmemcpy(it->ext, val->ptr, val->len + 1);
	ffstr_free(val);
	return 0;
}

static const ffpars_arg fmed_conf_inmap_args[] = {
	{ "*",  FFPARS_TSTR | FFPARS_FNOTEMPTY | FFPARS_FSTRZ | FFPARS_FCOPY | FFPARS_FMULTI, FFPARS_DST(&fmed_conf_inmap_val) }
};


static int fmed_conf_inputmap(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, fmed, fmed_conf_inmap_args, FFCNT(fmed_conf_inmap_args));
	return 0;
}

static int fmed_conf(void)
{
	ffparser pconf;
	ffparser_schem ps;
	const ffpars_ctx ctx = { fmed, fmed_conf_args, FFCNT(fmed_conf_args), NULL, {0} };
	fffd fd;
	fffilemap fm;
	int r = -1;
	size_t maplen;
	ffstr mp;
	char *filename;

	ffconf_scheminit(&ps, &pconf, &ctx);

	if (NULL == (filename = core->getpath(FFSTR("fmedia.conf"))))
		return -1;

	fd = fffile_open(filename, O_RDONLY);
	if (fd == FF_BADFD) {
		syserrlog(core, NULL, "core", "%e: %s", FFERR_FOPEN, filename);
		goto fail;
	}

	fffile_mapinit(&fm);
	fffile_mapset(&fm, 64 * 1024, fd, 0, fffile_size(fd));

	for (;;) {
		if (0 != fffile_mapbuf(&fm, &mp)) {
			syserrlog(core, NULL, "core", "%e: %s", FFERR_FMAP, filename);
			goto fail;
		}
		maplen = mp.len;

		while (mp.len != 0) {
			size_t nn = mp.len;
			r = ffconf_parse(ps.p, mp.ptr, &nn);
			ffstr_shift(&mp, nn);

			if (r == FFPARS_MORE) {
				if (0 != ffpars_savedata(ps.p))
					goto err;
				break;
			}

			if (ffpars_iserr(r))
				goto err;

			r = ffpars_schemrun(&ps, r);
			if (ffpars_iserr(r))
				goto err;
		}

		if (0 == fffile_mapshift(&fm, maplen))
			break;
	}

	if (!ffpars_iserr(r))
		r = ffconf_schemfin(&ps);

err:
	if (ffpars_iserr(r)) {
		const char *ser = ffpars_schemerrstr(&ps, r, NULL, 0);
		errlog(core, NULL, "core"
			, "parse config: %s: %u:%u: near \"%S\": \"%s\": %s"
			, filename
			, (int)ps.p->line, (int)ps.p->ch
			, &ps.p->val, (ps.curarg != NULL) ? ps.curarg->name : ""
			, ser);
		goto fail;
	}

	if (ps.p->ctxs.len != 1) {
		errlog(core, NULL, "core", "parse config: %s: incomplete document", filename);
		goto fail;
	}

	r = 0;

fail:
	ffmem_free(filename);
	fffile_mapclose(&fm);
	fffile_close(fd);
	ffpars_free(&pconf);
	ffpars_schemfree(&ps);
	return r;
}


void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va)
{
	char buf[4096];
	char *s = buf;
	const char *end = buf + FFCNT(buf) - FFSLEN("\n");
	char time_s[32];

	if (stime == NULL) {
		ffdtm dt;
		fftime t;
		size_t r;
		fftime_now(&t);
		fftime_split(&dt, &t, FFTIME_TZLOCAL);
		r = fftime_tostr(&dt, time_s, sizeof(time_s), FFTIME_HMS_MSEC);
		time_s[r] = '\0';
		stime = time_s;
	}

	s += ffs_fmt(s, end, "%s %s %s: ", stime, level, module);

	if (id != NULL)
		s += ffs_fmt(s, end, "%S:\t", id);

	s += ffs_fmtv(s, end, fmt, va);

	*s++ = '\n';

	ffstd_write(fd, buf, s - buf);
}


#ifdef FF_MSVC
enum {
	SIGINT = 1
};
#endif

static const int sigs[] = { SIGINT };

static void fmed_onsig(void *udata)
{
	core->sig(FMED_STOP);
}

#ifdef FF_WIN
static BOOL __stdcall fmed_ctrlhandler(DWORD ctrl)
{
	if (ctrl == CTRL_C_EVENT) {
		fmed_onsig(NULL);
		ffkqu_post(fmed->kq, &fmed->evposted, NULL);
		return 1;
	}
	return 0;
}
#endif

int main(int argc, char **argv)
{
	ffsignal sigs_task;

	ffmem_init();
	ffutf8_init();
	ffsig_init(&sigs_task);

	fffile_writecz(ffstdout, "fmedia ver. " FMED_VER "\n");

	if (0 != core_init())
		return 1;

	{
	char fn[FF_MAXPATH];
	const char *p = ffps_filename(fn, sizeof(fn), argv[0]);
	if (p == NULL)
		return 1;
	if (NULL == ffstr_copy(&fmed->root, p, ffsz_len(p)))
		return 1;
	ffpath_split2(fmed->root.ptr, fmed->root.len, &fmed->root, NULL);
	fmed->root.len += FFSLEN("/");
	}

	if (argc == 1) {
		fmed_arg_usage();
		return 0;
	}

	if (0 != fmed_conf())
		goto end;

	if (0 != fmed_cmdline(argc, argv))
		goto end;

	if (0 != core->sig(FMED_OPEN))
		goto end;

	if (0 != ffsig_ctl(&sigs_task, fmed->kq, sigs, FFCNT(sigs), &fmed_onsig)) {
		syserrlog(core, NULL, "core", "%s", "ffsig_ctl()");
		goto end;
	}

#ifdef FF_WIN
	SetConsoleCtrlHandler(&fmed_ctrlhandler, TRUE);
#endif

	core->sig(FMED_START);

end:
	ffsig_ctl(&sigs_task, fmed->kq, sigs, FFCNT(sigs), NULL);
	core_free();
	return 0;
}
