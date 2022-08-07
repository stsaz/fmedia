/** File output.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>
#include <util/filewrite.h>
#include <util/path.h>
#include <util/svar.h>
#include <FFOS/file.h>
#include <FFOS/dir.h>


extern const fmed_core *core;

#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "file", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "file", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "file", __VA_ARGS__)

extern ffthpool* thpool_create();


//OUTPUT
static void* fileout_open(fmed_filt *d);
static int fileout_write(void *ctx, fmed_filt *d);
static void fileout_close(void *ctx);
int fileout_config(fmed_conf_ctx *ctx);
const fmed_filter fmed_file_output = {
	&fileout_open, &fileout_write, &fileout_close
};

struct file_out_conf_t {
	uint counter;

	size_t bsize;
	size_t prealloc;
	uint file_del :1;
	uint prealloc_grow :1;
	byte use_thread_pool;
};
static struct file_out_conf_t out_conf;

static const fmed_conf_arg file_out_conf_args[] = {
	{ "use_thread_pool",	FMC_BOOL8,  FMC_O(struct file_out_conf_t, use_thread_pool) },
	{ "buffer_size",  FMC_SIZENZ,  FMC_O(struct file_out_conf_t, bsize) }
	, { "preallocate",  FMC_SIZENZ,  FMC_O(struct file_out_conf_t, prealloc) },
	{}
};


typedef struct fmed_fileout {
	fffilewrite *fw;
	fmed_trk *d;
	void *trk;
	ffstr fname;
	uint64 wr;
	fftime modtime;
	uint ok :1;
} fmed_fileout;

static char* fileout_getname(fmed_fileout *f, fmed_filt *d);

int fileout_config(fmed_conf_ctx *ctx)
{
	out_conf.bsize = 64 * 1024;
	out_conf.prealloc = 1 * 1024 * 1024;
	out_conf.prealloc_grow = 1;
	out_conf.file_del = 1;
	fmed_conf_addctx(ctx, &out_conf, file_out_conf_args);
	return 0;
}

enum VARS {
	VAR_COUNTER,
	VAR_DATE,
	VAR_FNAME,
	VAR_FPATH,
	VAR_TIME,
	VAR_TIMEMS,
	VAR_YEAR,
};

static const char* const vars[] = {
	"counter",
	"date",
	"filename",
	"filepath",
	"time",
	"timems",
	"year",
};

/** All printable except *, ?, /, \\, :, \", <, >, |. */
static const uint _ffpath_charmask_filename[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x2bff7bfb, // 0010 1011 1111 1111  0111 1011 1111 1011
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xefffffff, // 1110 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x6fffffff, // 0110 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};

size_t ffpath_makefn(char *dst, size_t dstcap, const char *src, size_t len, int repl_with)
{
	size_t i;
	const char *dsto = dst;
	const char *end = dst + dstcap;

	const char *pos = ffs_rskip(src, len, ' ');
	len = pos - src;

	for (i = 0;  i < len && dst != end;  i++) {
		if (!ffbit_testarr(_ffpath_charmask_filename, (byte)src[i]))
			*dst = (byte)repl_with;
		else
			*dst = src[i];
		dst++;
	}
	return dst - dsto;
}

static FFINL char* fileout_getname(fmed_fileout *f, fmed_filt *d)
{
	ffstr fn = FFSTR_INITZ(d->out_filename), val, fdir, fname, ext;
	char *tstr;
	const char *in;
	ffarr buf = {0}, outfn = {0};
	int r, have_dt = 0, ivar;
	ffdatetime dt = {};

	// "PATH/.EXT" -> "PATH/$filename.EXT"
	if (NULL == ffpath_split2(fn.ptr, fn.len, &fdir, &fname))
		ffstr_set(&fdir, ".", 1);
	if (0 == ffpath_splitname(fname.ptr, fname.len, &ext, NULL)) {
		if (0 == ffstr_catfmt(&outfn, "%S/$filename%S"
			, &fdir, &ext))
			goto done;
		ffstr_set2(&fn, &outfn);
	}

	while (fn.len != 0) {
		r = svar_split(&fn, &val);

		switch (r) {
		case FFSVAR_S:
			if (0 > (ivar = ffszarr_findsorted(vars, FFCNT(vars), val.ptr, val.len))) {
				if (FMED_PNULL == (tstr = d->track->getvalstr3(d->trk, &val, FMED_TRK_META | FMED_TRK_NAMESTR)))
					continue;
				ffstr_setz(&val, tstr);
				goto data;
			}

			switch (ivar) {

			case VAR_FPATH:
			case VAR_FNAME:
				if (NULL == (in = d->track->getvalstr(d->trk, "input")))
					goto done;
				ffpath_split2(in, ffsz_len(in), &fdir, &fname);
				break;

			case VAR_DATE:
			case VAR_TIME:
			case VAR_TIMEMS:
				if (!have_dt) {
					// get time only once
					fftime t;
					fftime_now(&t);
					uint tzoff = core->cmd(FMED_TZOFFSET);
					t.sec += FFTIME_1970_SECONDS + tzoff;
					fftime_split1(&dt, &t);
					have_dt = 1;
				}
				break;
			}

			switch (ivar) {

			case VAR_FPATH:
				if (fdir.len == 0)
					goto done;
				if (0 == ffvec_addstr(&buf, &fdir))
					goto syserr;
				break;

			case VAR_FNAME:
				ffpath_splitname(fname.ptr, fname.len, &val, NULL);
				goto data;

			case VAR_DATE:
				if (0 == ffstr_catfmt(&buf, "%04u%02u%02u", dt.year, dt.month, dt.day))
					goto syserr;
				break;

			case VAR_TIME:
				if (0 == ffstr_catfmt(&buf, "%02u%02u%02u", dt.hour, dt.minute, dt.second))
					goto syserr;
				break;

			case VAR_TIMEMS:
				if (0 == ffstr_catfmt(&buf, "%02u%02u%02u-%03u", dt.hour, dt.minute, dt.second, dt.nanosecond/1000000))
					goto syserr;
				break;

			case VAR_YEAR:
				ffstr_setcz(&val, "date");
				if (FMED_PNULL == (tstr = d->track->getvalstr3(d->trk, &val, FMED_TRK_META | FMED_TRK_NAMESTR)))
					continue;
				ffstr_setz(&val, tstr);
				goto data;

			case VAR_COUNTER:
				if (0 == ffstr_catfmt(&buf, "%u", ++out_conf.counter))
					goto syserr;
				break;
			}

			continue;

		case FFSVAR_TEXT:
			break;

		default:
			goto done;
		}

data:
		if (val.len == 0)
			continue;

		if (NULL == ffvec_grow(&buf, val.len, 1))
			goto syserr;

		switch (r) {
		case FFSVAR_S:
			r = ffpath_makefn(ffslice_end(&buf, 1), -1, val.ptr, val.len, '_');
			buf.len += r;
			break;

		case FFSVAR_TEXT:
			ffvec_addstr(&buf, &val);
			break;
		}
	}

	if (0 == ffvec_addchar(&buf, '\0'))
		goto syserr;
	ffstr_acqstr3(&f->fname, (ffarr*)&buf);
	f->fname.len--;

	if (!ffstr_eq2(&f->fname, &fn))
		d->track->setvalstr(d->trk, "output_expanded", f->fname.ptr);

	return f->fname.ptr;

syserr:
	syserrlog(d->trk, "%s", ffmem_alloc_S);

done:
	ffarr_free(&outfn);
	ffarr_free(&buf);
	return NULL;
}

static void fileout_log(void *p, uint level, ffstr msg)
{
	fmed_fileout *f = p;
	switch (level) {
	case FFFILEWRITE_LOG_ERR:
		errlog(f->trk, "%S", &msg);
		break;
	case FFFILEWRITE_LOG_DBG:
		dbglog(f->trk, "%S", &msg);
		break;
	}
}

static void fo_onwrite(void *udata)
{
	fmed_fileout *f = udata;
	f->d->track->cmd(f->trk, FMED_TRACK_WAKE);
}

static void* fileout_open(fmed_filt *d)
{
	const char *filename;
	fmed_fileout *f = ffmem_tcalloc1(fmed_fileout);
	if (f == NULL)
		return NULL;

	if (NULL == (filename = fileout_getname(f, d)))
		goto done;

	fffilewrite_conf conf;
	fffilewrite_setconf(&conf);
	conf.udata = f;
	conf.onwrite = &fo_onwrite;
	conf.log = &fileout_log;
	conf.log_debug = (core->loglev == FMED_LOG_DEBUG);
	if (out_conf.use_thread_pool)
		conf.thpool = thpool_create();
	conf.bufsize = out_conf.bsize;
	int64 n;
	if (FMED_NULL != (n = fmed_popval("out_bufsize")))
		conf.bufsize = n;
	conf.bufsize = out_conf.bsize;
	conf.prealloc = ((int64)d->output.size != FMED_NULL) ? d->output.size : out_conf.prealloc;
	conf.prealloc_grow = out_conf.prealloc_grow;
	conf.del_on_err = out_conf.file_del;
	conf.overwrite = d->out_overwrite;
	if (NULL == (f->fw = fffilewrite_create(filename, &conf)))
		goto done;

	f->modtime = d->mtime;
	f->d = d;
	f->trk = d->trk;
	return f;

done:
	fileout_close(f);
	return NULL;
}

static void fileout_close(void *ctx)
{
	fmed_fileout *f = ctx;

	if (f->fw != NULL) {
		fffilewrite_stat st;
		fffilewrite_getstat(f->fw, &st);
		fffilewrite_free(f->fw);

		if (f->ok) {
			if (f->d->out_file_del) {
				if (0 == fffile_rm(f->fname.ptr))
					dbglog(NULL, "removed file %S", &f->fname);
			} else {
				if (fftime_sec(&f->modtime) != 0)
					fffile_settimefn(f->fname.ptr, &f->modtime);

				core->log(FMED_LOG_USER, NULL, "file", "saved file %S, %U kbytes"
					, &f->fname, f->wr / 1024);
			}

			dbglog(NULL, "%S: mem write#:%u  file write#:%u  prealloc#:%u"
				, &f->fname, st.nmwrite, st.nfwrite, st.nprealloc);
		}
	}

	ffstr_free(&f->fname);
	ffmem_free(f);
}

static int fileout_write(void *ctx, fmed_filt *d)
{
	fmed_fileout *f = ctx;

	int64 seek = -1;
	if ((int64)d->output.seek != FMED_NULL) {
		seek = d->output.seek;
		d->output.seek = FMED_NULL;

		dbglog(d->trk, "seeking to %xU...", seek);
	}

	uint flags = 0;
	if (d->flags & FMED_FLAST)
		flags = FFFILEWRITE_FFLUSH;

	ffstr in;
	ffstr_set(&in, d->data, d->datalen);
	for (;;) {
		ssize_t r = fffilewrite_write(f->fw, in, seek, flags);
		switch (r) {
		default:
			if (r == 0 && (d->flags & FMED_FLAST)) {
				f->ok = 1;
				d->outlen = 0;
				return FMED_RDONE;
			}

			d->data += r;
			d->datalen -= r;
			ffstr_shift(&in, r);
			f->wr += r;

			if (in.len == 0 && !(d->flags & FMED_FLAST)) {
				d->outlen = 0;
				return FMED_ROK;
			}

			seek = -1;
			continue;

		case FFFILEWRITE_RERR:
			return FMED_RERR;

		case FFFILEWRITE_RASYNC:
			return FMED_RASYNC;
		}
	}

	return FMED_RERR;
}
