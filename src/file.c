/** File input/output.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/array.h>
#include <FFOS/file.h>
#include <FFOS/asyncio.h>
#include <FFOS/error.h>


static const fmed_core *core;


static struct file_in_conf_t {
	uint nbufs;
	size_t bsize;
	size_t align;
} file_in_conf;

static struct file_out_conf_t {
	size_t bsize;
	size_t prealoc;
} file_out_conf;

typedef struct databuf {
	char *ptr;
	uint off;
	uint len;
} databuf;

typedef struct fmed_file {
	const char *fn;
	fffd fd;
	uint wdata;
	uint rdata;
	databuf *data;
	uint64 foff;
	ffaio_filetask ftask;
	uint64 seek;

	fmed_handler handler;
	void *trk;

	unsigned async :1
		, done :1
		, want_read :1
		, err :1
		, out :1;
} fmed_file;

typedef struct fmed_fileout {
	ffstr fname;
	fffd fd;
	ffarr buf;
	uint64 fsize
		, prealocated;
} fmed_fileout;


//FMEDIA MODULE
static const fmed_filter* file_iface(const char *name);
static int file_sig(uint signo);
static void file_destroy(void);
static const fmed_mod fmed_file_mod = {
	&file_iface, &file_sig, &file_destroy
};

//INPUT
static void* file_open(fmed_filt *d);
static int file_getdata(void *ctx, fmed_filt *d);
static void file_close(void *ctx);
static int file_conf(ffpars_ctx *ctx);
static const fmed_filter fmed_file_input = {
	&file_open, &file_getdata, &file_close, &file_conf
};

static void file_read(void *udata);

static const ffpars_arg file_in_conf_args[] = {
	{ "buffer_size",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_in_conf_t, bsize) }
	, { "buffers",  FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(struct file_in_conf_t, nbufs) }
	, { "align",  FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(struct file_in_conf_t, align) }
};


//OUTPUT
static void* fileout_open(fmed_filt *d);
static int fileout_write(void *ctx, fmed_filt *d);
static void fileout_close(void *ctx);
static int fileout_config(ffpars_ctx *ctx);
static const fmed_filter fmed_file_output = {
	&fileout_open, &fileout_write, &fileout_close, &fileout_config
};

static const ffpars_arg file_out_conf_args[] = {
	{ "buffer_size",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, bsize) }
	, { "preallocate",  FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(struct file_out_conf_t, prealoc) }
};


const fmed_mod* fmed_getmod_file(const fmed_core *_core)
{
	if (0 != ffaio_fctxinit())
		return NULL;
	core = _core;
	return &fmed_file_mod;
}


static const fmed_filter* file_iface(const char *name)
{
	if (!ffsz_cmp(name, "in")) {
		file_in_conf.align = 4096;
		file_in_conf.bsize = 64 * 1024;
		file_in_conf.nbufs = 2;
		return &fmed_file_input;

	} else if (!ffsz_cmp(name, "out")) {
		file_out_conf.bsize = 64 * 1024;
		file_out_conf.prealoc = 2 * 1024 * 1024;
		return &fmed_file_output;
	}
	return NULL;
}

static int file_sig(uint signo)
{
	return 0;
}

static void file_destroy(void)
{
	ffaio_fctxclose();
}


static int file_conf(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &file_in_conf, file_in_conf_args, FFCNT(file_in_conf_args));
	return 0;
}

static void* file_open(fmed_filt *d)
{
	fmed_file *f;
	uint i;

	f = ffmem_tcalloc1(fmed_file);
	if (f == NULL)
		return NULL;
	f->fd = FF_BADFD;
	f->fn = d->track->getvalstr(d->trk, "input");
	if (NULL == (f->data = ffmem_tcalloc(databuf, file_in_conf.nbufs)))
		goto done;

	f->fd = fffile_opendirect(f->fn, O_RDONLY | O_NONBLOCK | O_NOATIME | FFO_NODOSNAME);
	if (f->fd == FF_BADFD) {
		syserrlog(core, d->trk, "file", "%e: %s", FFERR_FOPEN, f->fn);
		goto done;
	}

	dbglog(core, d->trk, "file", "opened %s (%U kbytes)", f->fn, fffile_size(f->fd) / 1024);

	ffaio_finit(&f->ftask, f->fd, f);
	f->ftask.kev.udata = f;
	if (0 != ffaio_fattach(&f->ftask, core->kq)) {
		syserrlog(core, d->trk, "file", "%e: %s", FFERR_KQUATT, f->fn);
		goto done;
	}

	for (i = 0;  i < file_in_conf.nbufs;  i++) {
		f->data[i].ptr = ffmem_align(file_in_conf.bsize, file_in_conf.align);
		if (f->data[i].ptr == NULL) {
			syserrlog(core, d->trk, "file", "%e: %s", FFERR_BUFALOC, f->fn);
			goto done;
		}
	}

	d->track->setval(d->trk, "total_size", fffile_size(f->fd));

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
		for (i = 0;  i < file_in_conf.nbufs;  i++) {
			if (f->data[i].ptr != NULL)
				ffmem_alignfree(f->data[i].ptr);
		}
		ffmem_free(f->data);
	}
	ffmem_free(f);
}

static int file_getdata(void *ctx, fmed_filt *d)
{
	fmed_file *f = ctx;
	int64 seek;
	uint i;

	if (f->err)
		return FMED_RERR;

	seek = d->track->popval(d->trk, "input_seek");
	if (seek != FMED_NULL) {
		f->seek = seek;
		if (f->seek >= (uint64)fffile_size(f->fd)) {
			errlog(core, d->trk, "file", "too big seek position %U", f->seek);
			return FMED_RERR;
		}

		f->wdata = 0;
		f->rdata = 0;
		f->out = 0;
		f->done = 0;
		if (!f->async) {
			f->foff = f->seek;
			f->seek = 0;
		}

		//reset all buffers
		for (i = 0;  i < file_in_conf.nbufs;  i++) {
			f->data[i].len = 0;
			f->data[i].off = 0;
		}
	}

	if (f->out) {
		f->out = 0;
		f->data[f->rdata].len = 0;
		f->data[f->rdata].off = 0;
		f->rdata = (f->rdata + 1) % file_in_conf.nbufs;

		if (f->done && f->data[f->rdata].len == 0) {
			/* We finished reading in the previous iteration.
			After that, noone's asked to seek back. */
			d->outlen = 0;
			return FMED_RDONE;
		}
	}

	if (!f->async && !f->done)
		file_read(f);

	if (f->data[f->rdata].len == 0 && !f->done) {
		f->want_read = 1;
		return FMED_RASYNC; //wait until the buffer is full
	}

	d->out = f->data[f->rdata].ptr;
	d->outlen = f->data[f->rdata].len;
	if (f->data[f->rdata].off != 0) {
		d->out += f->data[f->rdata].off;
		d->outlen -= f->data[f->rdata].off;
	}
	f->out = 1;

	return FMED_ROK;
}

static void file_read(void *udata)
{
	fmed_file *f = udata;
	uint filled = 0;
	int r;

	if (f->async && f->fd == FF_BADFD) {
		f->async = 0;
		file_close(f);
		return;
	}

	for (;;) {
		uint64 off = f->foff;
		if (f->foff % file_in_conf.align)
			off &= ~(file_in_conf.align-1);

		if (f->data[f->wdata].len != 0)
			break;

		r = (int)ffaio_fread(&f->ftask, f->data[f->wdata].ptr, file_in_conf.bsize, off, &file_read);
		f->async = 0;
		if (r < 0) {
			if (fferr_again(fferr_last())) {
				f->async = 1;
				break;
			} else if (fferr_last() == ERROR_HANDLE_EOF) {
				f->done = 1;
				break;
			}

			syserrlog(core, f->trk, "file", "%e: %s", FFERR_READ, f->fn);
			f->err = 1;
			return;
		}

		if (f->seek != 0) {
			f->foff = f->seek;
			f->seek = 0;
			continue;
		}

		if (r != file_in_conf.bsize) {
			dbglog(core, f->trk, "file", "reading's done", 0);
			f->done = 1;
		}

		dbglog(core, f->trk, "file", "read %U bytes at offset %U", (int64)r - (f->foff % file_in_conf.align), f->foff);
		if (f->foff % file_in_conf.align != 0)
			f->data[f->wdata].off = f->foff % file_in_conf.align;
		f->foff = (f->foff & ~(file_in_conf.align-1)) + r;
		f->data[f->wdata].len = r;
		filled = 1;

		f->wdata = (f->wdata + 1) % file_in_conf.nbufs;
		if (f->data[f->wdata].len != 0 || f->done)
			break; //all buffers are filled or end-of-file is reached
	}

	if (filled && f->want_read) {
		f->want_read = 0;
		f->handler(f->trk);
	}
}


static int fileout_config(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, &file_out_conf, file_out_conf_args, FFCNT(file_out_conf_args));
	return 0;
}

static void* fileout_open(fmed_filt *d)
{
	const char *filename;
	uint mode;
	uint64 total_size;
	fmed_fileout *f = ffmem_tcalloc1(fmed_fileout);
	if (f == NULL)
		return NULL;

	filename = d->track->getvalstr(d->trk, "output");

	if (NULL == ffstr_copy(&f->fname, filename, ffsz_len(filename) + 1)) {
		syserrlog(core, d->trk, "file", "%e", FFERR_BUFALOC);
		goto done;
	}

	mode = FFO_CREATENEW;
	if (FMED_NULL != d->track->getval(d->trk, "overwrite"))
		mode = O_CREAT;

	f->fd = fffile_open(filename, mode | O_WRONLY | O_NOATIME);
	if (f->fd == FF_BADFD) {
		syserrlog(core, d->trk, "file", "%e: %s", FFERR_FOPEN, filename);
		goto done;
	}

	if (NULL == ffarr_alloc(&f->buf, file_out_conf.bsize)) {
		syserrlog(core, d->trk, "file", "%e", FFERR_BUFALOC);
		goto done;
	}

	total_size = (uint64)d->track->getval(d->trk, "output_size");
	if (total_size != FMED_NULL) {
		if (0 == fffile_trunc(f->fd, total_size))
			f->prealocated = total_size;
	}

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
		if (0 != fffile_close(f->fd))
			syserrlog(core, NULL, "file", "%e", FFERR_FCLOSE);

		dbglog(core, NULL, "file", "saved file %s, %U kbytes"
			, f->fname.ptr, f->fsize / 1024);
	}

	ffstr_free(&f->fname);
	ffarr_free(&f->buf);
	ffmem_free(f);
}

static int fileout_write(void *ctx, fmed_filt *d)
{
	fmed_fileout *f = ctx;
	ssize_t r;
	ffstr dst;
	int64 seek;

	seek = d->track->popval(d->trk, "output_seek");
	if (seek != FMED_NULL) {
		if (0 > fffile_seek(f->fd, seek, SEEK_SET)) {
			syserrlog(core, d->trk, "file", "%e: %s", FFERR_FSEEK, f->fname.ptr);
			return -1;
		}

		if (d->datalen != fffile_write(f->fd, d->data, d->datalen)) {
			syserrlog(core, d->trk, "file", "%e: %s", FFERR_WRITE, f->fname.ptr);
			return -1;
		}

		dbglog(core, d->trk, "file", "written %L bytes at offset %U", d->datalen, seek);

		if (f->fsize < d->datalen)
			f->fsize = d->datalen;

		if (0 > fffile_seek(f->fd, f->fsize, SEEK_SET)) {
			syserrlog(core, d->trk, "file", "%e: %s", FFERR_FSEEK, f->fname.ptr);
			return -1;
		}

		d->datalen = 0;
	}

	for (;;) {

		r = ffbuf_add(&f->buf, d->data, d->datalen, &dst);
		d->data += r;
		d->datalen -= r;
		if (dst.len == 0) {
			if (!(d->flags & FMED_FLAST))
				break;
			ffstr_set(&dst, f->buf.ptr, f->buf.len);
		}

		if (f->fsize + d->datalen > f->prealocated) {
			if (0 == fffile_trunc(f->fd, f->prealocated + file_out_conf.prealoc))
				f->prealocated += file_out_conf.prealoc;
		}

		r = fffile_write(f->fd, dst.ptr, dst.len);
		if (r != dst.len) {
			syserrlog(core, d->trk, "file", "%e: %s", FFERR_WRITE, f->fname.ptr);
			return -1;
		}

		dbglog(core, d->trk, "file", "written %L bytes at offset %U", r, f->fsize);
		f->fsize += r;
		if (d->datalen == 0)
			break;
	}

	if (d->flags & FMED_FLAST)
		return FMED_RDONE;

	return FMED_ROK;
}
