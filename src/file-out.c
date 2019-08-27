/** File output.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>

#include <FF/time.h>
#include <FF/path.h>
#include <FFOS/file.h>
#include <FFOS/dir.h>


extern const fmed_core *core;

#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "file", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "file", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "file", __VA_ARGS__)


//OUTPUT
static void* fileout_open(fmed_filt *d);
static int fileout_write(void *ctx, fmed_filt *d);
static void fileout_close(void *ctx);
int fileout_config(ffpars_ctx *ctx);
const fmed_filter fmed_file_output = {
	&fileout_open, &fileout_write, &fileout_close
};

struct file_out_conf_t {
	size_t bsize;
	size_t prealloc;
	uint file_del :1;
	uint prealloc_grow :1;
};
static struct file_out_conf_t out_conf;

static const ffpars_arg file_out_conf_args[] = {
	{ "buffer_size",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, bsize) }
	, { "preallocate",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, prealloc) }
};


typedef struct fmed_fileout {
	fmed_trk *d;
	ffstr fname;
	fffd fd;
	ffarr buf;
	uint64 fsize
		, preallocated;
	uint64 prealloc_by;
	fftime modtime;
	uint ok :1;

	struct {
		uint nmwrite;
		uint nfwrite;
		uint nprealloc;
	} stat;
} fmed_fileout;

static int fileout_writedata(fmed_fileout *f, const char *data, size_t len, fmed_filt *d);
static char* fileout_getname(fmed_fileout *f, fmed_filt *d);


int fileout_config(ffpars_ctx *ctx)
{
	out_conf.bsize = 64 * 1024;
	out_conf.prealloc = 1 * 1024 * 1024;
	out_conf.prealloc_grow = 1;
	out_conf.file_del = 1;
	ffpars_setargs(ctx, &out_conf, file_out_conf_args, FFCNT(file_out_conf_args));
	return 0;
}

enum VARS {
	VAR_DATE,
	VAR_FNAME,
	VAR_FPATH,
	VAR_TIME,
	VAR_TIMEMS,
	VAR_YEAR,
};

static const char* const vars[] = {
	"date",
	"filename",
	"filepath",
	"time",
	"timems",
	"year",
};

static FFINL char* fileout_getname(fmed_fileout *f, fmed_filt *d)
{
	ffsvar p;
	ffstr fn, val, fdir, fname, ext;
	char *tstr;
	const char *in;
	ffarr buf = {0}, outfn = {0};
	int r, have_dt = 0, ivar;
	ffdtm dt;

	ffmem_tzero(&p);
	ffstr_setz(&fn, d->track->getvalstr(d->trk, "output"));

	// "PATH/.EXT" -> "PATH/$filename.EXT"
	if (NULL == ffpath_split2(fn.ptr, fn.len, &fdir, &fname))
		ffstr_set(&fdir, ".", 1);
	if (fname.ptr == ffpath_splitname(fname.ptr, fname.len, &ext, NULL)) {
		if (0 == ffstr_catfmt(&outfn, "%S/$filename%S"
			, &fdir, &ext))
			goto done;
		ffstr_set2(&fn, &outfn);
	}

	while (fn.len != 0) {
		size_t n = fn.len;
		r = ffsvar_parse(&p, fn.ptr, &n);
		ffstr_shift(&fn, n);

		switch (r) {
		case FFSVAR_S:
			if (0 > (ivar = ffszarr_findsorted(vars, FFCNT(vars), p.val.ptr, p.val.len))) {
				if (FMED_PNULL == (tstr = d->track->getvalstr3(d->trk, &p.val, FMED_TRK_META | FMED_TRK_NAMESTR)))
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
					fftime_split(&dt, &t, FFTIME_TZLOCAL);
					have_dt = 1;
				}
				break;
			}

			switch (ivar) {

			case VAR_FPATH:
				if (fdir.len == 0)
					goto done;
				if (NULL == ffarr_append(&buf, fdir.ptr, fdir.len))
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
				if (0 == ffstr_catfmt(&buf, "%02u%02u%02u", dt.hour, dt.min, dt.sec))
					goto syserr;
				break;

			case VAR_TIMEMS:
				if (0 == ffstr_catfmt(&buf, "%02u%02u%02u-%03u", dt.hour, dt.min, dt.sec, fftime_msec(&dt)))
					goto syserr;
				break;

			case VAR_YEAR:
				ffstr_setcz(&val, "date");
				if (FMED_PNULL == (tstr = d->track->getvalstr3(d->trk, &val, FMED_TRK_META | FMED_TRK_NAMESTR)))
					continue;
				ffstr_setz(&val, tstr);
				goto data;
			}

			continue;

		case FFSVAR_TEXT:
			val = p.val;
			break;

		default:
			goto done;
		}

data:
		if (val.len == 0)
			continue;

		if (NULL == ffarr_grow(&buf, val.len, 0))
			goto syserr;

		switch (r) {
		case FFSVAR_S:
			r = ffpath_makefn(ffarr_end(&buf), -1, val.ptr, val.len, '_');
			buf.len += r;
			break;

		case FFSVAR_TEXT:
			ffarr_append(&buf, val.ptr, val.len);
			break;
		}
	}

	if (NULL == ffarr_append(&buf, "", 1))
		goto syserr;
	ffstr_acqstr3(&f->fname, &buf);
	f->fname.len--;

/* Currently no one needs the expanded file name.  But "split" needs the original file name.
	if (!ffstr_eq2(&f->fname, &fn))
		d->track->setvalstr(d->trk, "output", f->fname.ptr);
*/

	return f->fname.ptr;

syserr:
	syserrlog(d->trk, "%s", ffmem_alloc_S);

done:
	ffarr_free(&outfn);
	ffarr_free(&buf);
	return NULL;
}

static void* fileout_open(fmed_filt *d)
{
	const char *filename;
	fmed_fileout *f = ffmem_tcalloc1(fmed_fileout);
	if (f == NULL)
		return NULL;
	f->fd = FF_BADFD;

	if (NULL == (filename = fileout_getname(f, d)))
		goto done;

	uint flags = (d->out_overwrite) ? O_CREAT : FFO_CREATENEW;
	flags |= O_WRONLY;
	f->fd = fffile_open(filename, flags);
	if (f->fd == FF_BADFD) {

		if (fferr_nofile(fferr_last())) {
			if (0 != ffdir_make_path((void*)filename, 0)) {
				syserrlog(d->trk, "%s: for filename %s", ffdir_make_S, filename);
				goto done;
			}

			f->fd = fffile_open(filename, flags);
		}

		if (f->fd == FF_BADFD) {
			syserrlog(d->trk, "%s: %s", fffile_open_S, filename);
			goto done;
		}
	}

	size_t bfsz = out_conf.bsize;
	int64 n;
	if (FMED_NULL != (n = fmed_popval("out_bufsize")))
		bfsz = n; //Note: a large value can slow down the thread because we write to a file synchronously
	if (NULL == ffarr_alloc(&f->buf, bfsz)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	if ((int64)d->output.size != FMED_NULL) {
		if (0 == fffile_trunc(f->fd, d->output.size)) {
			f->preallocated = d->output.size;
			f->stat.nprealloc++;
		}
	}

	f->modtime = d->mtime;
	f->prealloc_by = out_conf.prealloc;
	f->d = d;
	return f;

done:
	fileout_close(f);
	return NULL;
}

static void fileout_close(void *ctx)
{
	fmed_fileout *f = ctx;

	if (f->fd != FF_BADFD) {

		fffile_trunc(f->fd, f->fsize);

		if ((!f->ok && out_conf.file_del) || f->d->out_file_del) {

			if (0 != fffile_close(f->fd))
				syserrlog(NULL, "%s", fffile_close_S);

			if (0 == fffile_rm(f->fname.ptr))
				dbglog(NULL, "removed file %S", &f->fname);

		} else {

			if (fftime_sec(&f->modtime) != 0)
				fffile_settime(f->fd, &f->modtime);

			if (0 != fffile_close(f->fd))
				syserrlog(NULL, "%s", fffile_close_S);

			core->log(FMED_LOG_USER, NULL, "file", "saved file %S, %U kbytes"
				, &f->fname, f->fsize / 1024);
		}
	}

	ffstr_free(&f->fname);
	ffarr_free(&f->buf);
	dbglog(NULL, "mem write#:%u  file write#:%u  prealloc#:%u"
		, f->stat.nmwrite, f->stat.nfwrite, f->stat.nprealloc);
	ffmem_free(f);
}

static int fileout_writedata(fmed_fileout *f, const char *data, size_t len, fmed_filt *d)
{
	size_t r;
	if (f->prealloc_by != 0 && f->fsize + len > f->preallocated) {
		uint64 n = ff_align_ceil(f->fsize + len, f->prealloc_by);
		if (0 == fffile_trunc(f->fd, n)) {

			if (out_conf.prealloc_grow)
				f->prealloc_by += f->prealloc_by;

			f->preallocated = n;
			f->stat.nprealloc++;
		}
	}

	r = fffile_write(f->fd, data, len);
	if (r != len) {
		syserrlog(d->trk, "%s: %s", fffile_write_S, f->fname.ptr);
		return -1;
	}
	f->stat.nfwrite++;

	dbglog(d->trk, "written %L bytes at offset %U (%L pending)", r, f->fsize, d->datalen);
	f->fsize += r;
	return r;
}

static int fileout_write(void *ctx, fmed_filt *d)
{
	fmed_fileout *f = ctx;
	ssize_t r;
	ffstr dst;
	int64 seek;

	if ((int64)d->output.seek != FMED_NULL) {
		seek = d->output.seek;
		d->output.seek = FMED_NULL;

		if (f->buf.len != 0) {
			if (-1 == fileout_writedata(f, f->buf.ptr, f->buf.len, d))
				return FMED_RERR;
			f->buf.len = 0;
		}

		dbglog(d->trk, "seeking to %xU...", seek);

		if (0 > fffile_seek(f->fd, seek, SEEK_SET)) {
			syserrlog(d->trk, "%s: %s", fffile_seek_S, f->fname.ptr);
			return FMED_RERR;
		}

		if (d->datalen != (size_t)fffile_write(f->fd, d->data, d->datalen)) {
			syserrlog(d->trk, "%s: %s", fffile_write_S, f->fname.ptr);
			return FMED_RERR;
		}
		f->stat.nfwrite++;

		dbglog(d->trk, "written %L bytes at offset %U", d->datalen, seek);

		if (f->fsize < d->datalen)
			f->fsize = d->datalen;

		if (0 > fffile_seek(f->fd, f->fsize, SEEK_SET)) {
			syserrlog(d->trk, "%s: %s", fffile_seek_S, f->fname.ptr);
			return FMED_RERR;
		}

		d->datalen = 0;
	}

	for (;;) {

		r = ffbuf_add(&f->buf, d->data, d->datalen, &dst);
		d->data += r;
		d->datalen -= r;
		if (dst.len == 0) {
			f->stat.nmwrite++;
			if (!(d->flags & FMED_FLAST) || f->buf.len == 0)
				break;
			ffstr_set(&dst, f->buf.ptr, f->buf.len);
		}

		if (-1 == fileout_writedata(f, dst.ptr, dst.len, d))
			return FMED_RERR;
		if (d->datalen == 0)
			break;
	}

	if (d->flags & FMED_FLAST) {
		f->ok = 1;
		return FMED_RDONE;
	}

	return FMED_ROK;
}
