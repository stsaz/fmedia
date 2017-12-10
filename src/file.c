/** File input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/array.h>
#include <FF/time.h>
#include <FF/data/parse.h>
#include <FFOS/file.h>
#include <FFOS/asyncio.h>
#include <FFOS/error.h>
#include <FFOS/dir.h>
#include <FF/path.h>


#undef dbglog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "file", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "file", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "file", __VA_ARGS__)


struct file_in_conf_t {
	uint nbufs;
	size_t bsize;
	size_t align;
	byte directio;
};

struct file_out_conf_t {
	size_t bsize;
	size_t prealloc;
	uint file_del :1;
	uint prealloc_grow :1;
};

typedef struct filemod {
	struct file_in_conf_t in_conf;
	struct file_out_conf_t out_conf;
} filemod;

static filemod *mod;
static const fmed_core *core;

typedef struct databuf {
	char *ptr;
	uint64 off;
	uint len;
} databuf;

typedef struct fmed_file {
	const char *fn;
	fffd fd;

	uint wdata;
	uint rdata;
	uint unread_bufs;
	databuf *data;

	uint64 fsize;
	uint64 foff; //current read position
	ffaio_filetask ftask;
	int64 seek; //user's read position

	fmed_handler handler;
	void *trk;

	unsigned async :1
		, done :1
		, cancelled :1
		, want_read :1
		, err :1
		, out :1;
} fmed_file;

enum {
	FILEIN_MAX_PREBUF = 2, //maximum number of unread buffers
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

typedef struct stdin_ctx {
	fffd fd;
	ffarr buf;
} stdin_ctx;

typedef struct stdout_ctx {
	fffd fd;
	ffarr buf;
	uint64 fsize;

	struct {
		uint nmwrite;
		uint nfwrite;
	} stat;
} stdout_ctx;


//FMEDIA MODULE
static const void* file_iface(const char *name);
static int file_conf(const char *name, ffpars_ctx *ctx);
static int file_sig(uint signo);
static void file_destroy(void);
static const fmed_mod fmed_file_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&file_iface, &file_sig, &file_destroy, &file_conf
};

//INPUT
static void* file_open(fmed_filt *d);
static int file_getdata(void *ctx, fmed_filt *d);
static void file_close(void *ctx);
static int file_in_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_file_input = {
	&file_open, &file_getdata, &file_close
};

static void file_read(void *udata);

static const ffpars_arg file_in_conf_args[] = {
	{ "buffer_size",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_in_conf_t, bsize) }
	, { "buffers",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(struct file_in_conf_t, nbufs) }
	, { "align",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_in_conf_t, align) }
	, { "direct_io",  FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct file_in_conf_t, directio) }
};


//OUTPUT
static void* fileout_open(fmed_filt *d);
static int fileout_write(void *ctx, fmed_filt *d);
static void fileout_close(void *ctx);
static int fileout_config(ffpars_ctx *ctx);
static const fmed_filter fmed_file_output = {
	&fileout_open, &fileout_write, &fileout_close
};

static int fileout_writedata(fmed_fileout *f, const char *data, size_t len, fmed_filt *d);
static char* fileout_getname(fmed_fileout *f, fmed_filt *d);

static const ffpars_arg file_out_conf_args[] = {
	{ "buffer_size",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, bsize) }
	, { "preallocate",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, prealloc) }
};

//STDIN
static void* file_stdin_open(fmed_filt *d);
static int file_stdin_read(void *ctx, fmed_filt *d);
static void file_stdin_close(void *ctx);
static const fmed_filter file_stdin = {
	&file_stdin_open, &file_stdin_read, &file_stdin_close
};

//STDOUT
static void* file_stdout_open(fmed_filt *d);
static int file_stdout_write(void *ctx, fmed_filt *d);
static void file_stdout_close(void *ctx);
static const fmed_filter file_stdout = {
	&file_stdout_open, &file_stdout_write, &file_stdout_close
};


const fmed_mod* fmed_getmod_file(const fmed_core *_core)
{
	if (mod != NULL)
		return &fmed_file_mod;

	if (0 != ffaio_fctxinit())
		return NULL;
	core = _core;
	if (NULL == (mod = ffmem_tcalloc1(filemod)))
		return NULL;
	return &fmed_file_mod;
}


static const void* file_iface(const char *name)
{
	if (!ffsz_cmp(name, "in")) {
		return &fmed_file_input;
	} else if (!ffsz_cmp(name, "out")) {
		return &fmed_file_output;
	} else if (!ffsz_cmp(name, "stdin"))
		return &file_stdin;
	else if (!ffsz_cmp(name, "stdout"))
		return &file_stdout;
	return NULL;
}

static int file_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "in"))
		return file_in_conf(ctx);
	else if (!ffsz_cmp(name, "out"))
		return fileout_config(ctx);
	return -1;
}

static int file_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		break;
	}
	return 0;
}

static void file_destroy(void)
{
	ffaio_fctxclose();
	ffmem_free0(mod);
}


static int file_in_conf(ffpars_ctx *ctx)
{
	mod->in_conf.align = 4096;
	mod->in_conf.bsize = 64 * 1024;
	mod->in_conf.nbufs = 3;
	mod->in_conf.directio = 1;
	ffpars_setargs(ctx, &mod->in_conf, file_in_conf_args, FFCNT(file_in_conf_args));
	return 0;
}

static void* file_open(fmed_filt *d)
{
	fmed_file *f;
	uint i;
	fffileinfo fi;

	f = ffmem_tcalloc1(fmed_file);
	if (f == NULL)
		return NULL;
	f->fd = FF_BADFD;
	f->fn = d->track->getvalstr(d->trk, "input");

	uint flags = O_RDONLY | O_NOATIME | O_NONBLOCK | FFO_NODOSNAME;
	flags |= (mod->in_conf.directio) ? O_DIRECT : 0;
	for (;;) {
		f->fd = fffile_open(f->fn, flags);

#ifdef FF_LINUX
		if (f->fd == FF_BADFD && fferr_last() == EINVAL && (flags & O_DIRECT)) {
			flags &= ~O_DIRECT;
			continue;
		}
#endif

		break;
	}

	if (f->fd == FF_BADFD) {
		syserrlog(d->trk, "%s: %s", fffile_open_S, f->fn);
		goto done;
	}
	if (0 != fffile_info(f->fd, &fi)) {
		syserrlog(d->trk, "%s: %s", fffile_info_S, f->fn);
		goto done;
	}
	f->fsize = fffile_infosize(&fi);

	dbglog(d->trk, "opened %s (%U kbytes)", f->fn, f->fsize / 1024);

	ffaio_finit(&f->ftask, f->fd, f);
	f->ftask.kev.udata = f;
	if (0 != ffaio_fattach(&f->ftask, core->kq, !!(flags & O_DIRECT))) {
		syserrlog(d->trk, "%s: %s", ffkqu_attach_S, f->fn);
		goto done;
	}

	if (NULL == (f->data = ffmem_callocT(mod->in_conf.nbufs, databuf)))
		goto done;
	for (i = 0;  i != mod->in_conf.nbufs;  i++) {
		if (NULL == (f->data[i].ptr = ffmem_align(mod->in_conf.bsize, mod->in_conf.align))) {
			syserrlog(d->trk, "%s", ffmem_alloc_S);
			goto done;
		}
		f->data[i].off = (uint64)-1;
	}

	d->input.size = f->fsize;

	if (d->out_preserve_date) {
		d->mtime = fffile_infomtime(&fi);
	}

	f->handler = d->handler;
	f->trk = d->trk;
	return f;

done:
	file_close(f);
	return NULL;
}

static void file_close(void *ctx)
{
	fmed_file *f = ctx;
	uint i;

	if (f->fd != FF_BADFD) {
		fffile_close(f->fd);
		f->fd = FF_BADFD;
	}
	if (f->async)
		return; //wait until async operation is completed

	if (f->data != NULL) {
		for (i = 0;  i < mod->in_conf.nbufs;  i++) {
			if (f->data[i].ptr != NULL)
				ffmem_alignfree(f->data[i].ptr);
		}
		ffmem_free(f->data);
	}

	ffmem_free(f);
}

static databuf* find_buf(fmed_file *f, uint64 offset)
{
	databuf *b = f->data;
	for (uint i = 0;  i != mod->in_conf.nbufs;  i++, b++) {
		if (ffint_within(offset, b->off, b->off + b->len))
			return b;
	}
	return NULL;
}

static int file_getdata(void *ctx, fmed_filt *d)
{
	fmed_file *f = ctx;
	const databuf *b = NULL;

	if (f->out) {
		f->out = 0;
		f->rdata = ffint_cycleinc(f->rdata, mod->in_conf.nbufs);
		f->unread_bufs--;
	}

	if ((int64)d->input.seek != FMED_NULL) {
		uint64 seek = d->input.seek;
		d->input.seek = FMED_NULL;
		if (seek >= f->fsize) {
			errlog(d->trk, "too big seek position %U", seek);
			return FMED_RERR;
		}

		dbglog(d->trk, "seeking to %xU", seek);
		f->seek = seek;
		f->cancelled = f->async;

		if (NULL != (b = find_buf(f, seek))) {
			dbglog(d->trk, "hit cached buf#%u  offset:%xU"
				, b - f->data, b->off);
			f->rdata = b - f->data;
			f->wdata = ffint_cycleinc(f->rdata, mod->in_conf.nbufs);
			f->unread_bufs = 1;
			f->foff = b->off + b->len;

		} else {
			f->rdata = f->wdata;
			f->unread_bufs = 0;
			f->foff = ff_align_floor2(seek, mod->in_conf.align);
		}
		f->done = (f->foff >= f->fsize);
	}

	if (!f->async && !f->done)
		file_read(f);

	if (f->err)
		return FMED_RERR;

	if (f->unread_bufs == 0) {
		if (f->done) {
			/* We finished reading in the previous iteration.
			After that, noone's asked to seek back. */
			d->outlen = 0;
			return FMED_RDONE;
		}
		f->want_read = 1;
		return FMED_RASYNC; //wait until the buffer is full
	}

	b = &f->data[f->rdata];

	dbglog(d->trk, "returning buf#%u  offset:%xU  seek:%xD"
		, f->rdata, b->off, f->seek);

	d->out = b->ptr,  d->outlen = b->len;
	FF_ASSERT(ffint_within((uint64)f->seek, b->off, b->off + b->len));
	d->out += f->seek - b->off;
	d->outlen -= f->seek - b->off;
	f->seek = b->off + b->len;
	f->out = 1;
	return FMED_ROK;
}

static void file_read(void *udata)
{
	fmed_file *f = udata;
	int r;
	databuf *b;

	if (f->async && f->fd == FF_BADFD) {
		f->async = 0;
		file_close(f);
		return;
	}

	for (;;) {

		if (f->unread_bufs == ffmin(mod->in_conf.nbufs, FILEIN_MAX_PREBUF))
			break;

		b = &f->data[f->wdata];

		uint64 off;
		if (f->async)
			off = b->off;
		else {
			if (ffint_within(f->foff, b->off, b->off + b->len))
				goto ok; //this buffer already contains some of the needed data
			off = f->foff;
			b->off = f->foff;
			b->len = 0;
		}

		r = (int)ffaio_fread(&f->ftask, b->ptr, mod->in_conf.bsize, off, &file_read);
		f->async = 0;
		if (r < 0) {
			if (fferr_again(fferr_last())) {
				dbglog(f->trk, "buf#%u: async read, offset:%xU", f->wdata, off);
				b->len = 0;
				f->async = 1;
				break;
			}

			syserrlog(f->trk, "%s: %s  buf#%u offset:%xU"
				, fffile_read_S, f->fn, f->wdata, off);
			b->off = (uint64)-1;
			b->len = 0;
			f->err = 1;
			break;
		}

		b->len = r;
		dbglog(f->trk, "buf#%u: read %u bytes at offset %xU"
			, f->wdata, r, off);
		if ((uint)r != mod->in_conf.bsize) {
			dbglog(f->trk, "reading's done", 0);
			f->done = 1;
		}

		if (f->cancelled) {
			f->cancelled = 0;
			continue;
		}

		if (r == 0 && f->done)
			break;

ok:
		f->unread_bufs++;
		f->foff = b->off + b->len;
		f->done = (f->foff >= f->fsize);
		f->wdata = ffint_cycleinc(f->wdata, mod->in_conf.nbufs);
		if (f->wdata == f->rdata || f->done)
			break; //all buffers are filled or end-of-file is reached
	}

	if ((f->unread_bufs != 0 || f->err) && f->want_read) {
		f->want_read = 0;
		f->handler(f->trk);
	}
}


static int fileout_config(ffpars_ctx *ctx)
{
	mod->out_conf.bsize = 64 * 1024;
	mod->out_conf.prealloc = 1 * 1024 * 1024;
	mod->out_conf.prealloc_grow = 1;
	mod->out_conf.file_del = 1;
	ffpars_setargs(ctx, &mod->out_conf, file_out_conf_args, FFCNT(file_out_conf_args));
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
				if (0 == ffstr_catfmt(&buf, "%02u%02u%02u-%03u", dt.hour, dt.min, dt.sec, dt.msec))
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
			ffpath_makefn(ffarr_end(&buf), -1, val.ptr, val.len, '_');
			buf.len += val.len;
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

	if (!ffstr_eq2(&f->fname, &fn))
		d->track->setvalstr(d->trk, "output", f->fname.ptr);

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

	if (NULL == ffarr_alloc(&f->buf, mod->out_conf.bsize)) {
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
	f->prealloc_by = mod->out_conf.prealloc;
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

		if ((!f->ok && mod->out_conf.file_del) || f->d->out_file_del) {

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

			if (mod->out_conf.prealloc_grow)
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
			return -1;
		}

		if (d->datalen != (size_t)fffile_write(f->fd, d->data, d->datalen)) {
			syserrlog(d->trk, "%s: %s", fffile_write_S, f->fname.ptr);
			return -1;
		}
		f->stat.nfwrite++;

		dbglog(d->trk, "written %L bytes at offset %U", d->datalen, seek);

		if (f->fsize < d->datalen)
			f->fsize = d->datalen;

		if (0 > fffile_seek(f->fd, f->fsize, SEEK_SET)) {
			syserrlog(d->trk, "%s: %s", fffile_seek_S, f->fname.ptr);
			return -1;
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


static void* file_stdin_open(fmed_filt *d)
{
	stdin_ctx *f = ffmem_tcalloc1(stdin_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdin;

	if (NULL == ffarr_alloc(&f->buf, mod->in_conf.bsize)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdin_close(f);
	return NULL;
}

static void file_stdin_close(void *ctx)
{
	stdin_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdin_read(void *ctx, fmed_filt *d)
{
	stdin_ctx *f = ctx;
	ssize_t r;

	if ((int64)d->input.seek != FMED_NULL) {
		errlog(d->trk, "can't seek on stdin.  offset:%U", d->input.seek);
		return FMED_RERR;
	}

	r = ffstd_fread(f->fd, f->buf.ptr, f->buf.cap);
	if (r == 0) {
		d->outlen = 0;
		return FMED_RDONE;
	} else if (r < 0) {
		syserrlog(d->trk, "%s", fffile_read_S);
		return FMED_RERR;
	}

	dbglog(d->trk, "read %L bytes from stdin"
		, r);
	d->out = f->buf.ptr, d->outlen = r;
	return FMED_RDATA;
}


static void* file_stdout_open(fmed_filt *d)
{
	stdout_ctx *f = ffmem_tcalloc1(stdout_ctx);
	if (f == NULL)
		return NULL;
	f->fd = ffstdout;

	if (NULL == ffarr_alloc(&f->buf, mod->out_conf.bsize)) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	return f;

done:
	file_stdout_close(f);
	return NULL;
}

static void file_stdout_close(void *ctx)
{
	stdout_ctx *f = ctx;
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int file_stdout_writedata(stdout_ctx *f, const char *data, size_t len, fmed_filt *d)
{
	size_t r;
	r = fffile_write(f->fd, data, len);
	if (r != len) {
		syserrlog(d->trk, "%s", fffile_write_S);
		return -1;
	}
	f->stat.nfwrite++;

	dbglog(d->trk, "written %L bytes at offset %U (%L pending)", r, f->fsize, d->datalen);
	f->fsize += r;
	return r;
}

static int file_stdout_write(void *ctx, fmed_filt *d)
{
	stdout_ctx *f = ctx;
	ssize_t r;
	ffstr dst;

	if ((int64)d->output.seek != FMED_NULL) {

		if (f->buf.len != 0) {
			if (-1 == file_stdout_writedata(f, f->buf.ptr, f->buf.len, d))
				return FMED_RERR;
			f->buf.len = 0;
		}

		errlog(d->trk, "can't seek on stdout.  offset:%U", d->output.seek);
		return FMED_RERR;
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

		if (-1 == file_stdout_writedata(f, dst.ptr, dst.len, d))
			return FMED_RERR;

		if (d->datalen == 0)
			break;
	}

	if (d->flags & FMED_FLAST) {
		return FMED_RDONE;
	}

	return FMED_ROK;
}
