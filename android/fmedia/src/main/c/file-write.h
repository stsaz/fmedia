/** fmedia/Android: file-write filter
2022, Simon Zolin */

#include <util/fcache.h>
#include <FFOS/file.h>

#define ALIGN (4*1024)
#define BUF_SIZE (64*1024)

struct fo {
	fffd fd;
	uint64 off_cur, size;
	struct fcache_buf wbuf;
	fftime mtime;
	void *trk;
	const char *name;
	uint fin;
};

static void fo_close(void *ctx)
{
	struct fo *f = ctx;

	if (f->fd != FFFILE_NULL) {
		if (0 != fffile_close(f->fd)) {
			syserrlog1(f->trk, "file close: %s", f->name);
		} else {
			if (!f->fin) {
				if (0 == fffile_remove(f->name))
					dbglog1(f->trk, "removed file %s", f->name);
			} else {
				if (f->mtime.sec != 0) {
					fffile_set_mtime_path(f->name, &f->mtime);
				}
				dbglog1(f->trk, "%s: written %UKB", f->name, f->size / 1024);
			}
		}
	}

	ffmem_alignfree(f->wbuf.ptr);
	ffmem_free(f);
}

static void* fo_open(fmed_track_info *ti)
{
	struct fo *f = ffmem_new(struct fo);
	f->fd = FFFILE_NULL;
	f->trk = ti->trk;
	f->name = ti->out_filename;

	f->wbuf.ptr = ffmem_align(BUF_SIZE, ALIGN);

	uint flags = FFFILE_WRITEONLY;
	if (ti->out_overwrite)
		flags |= FFFILE_CREATE;
	else
		flags |= FFFILE_CREATENEW;
	if (FFFILE_NULL == (f->fd = fffile_open(f->name, flags))) {
		syserrlog1(ti->trk, "fffile_open: %s", f->name);
		if (fferr_exist(fferr_last()))
			ti->error = FMED_E_DSTEXIST;
		goto end;
	}

	f->mtime = ti->out_mtime;

	dbglog1(ti->trk, "%s: opened", f->name);
	return f;

end:
	fo_close(f);
	return NULL;
}

/** Pass data to kernel */
static int fo_write(struct fo *f, ffstr d, uint64 off)
{
	ffssize r = fffile_writeat(f->fd, d.ptr, d.len, off);
	if (r < 0) {
		syserrlog1(f->trk, "file write: %s %L @%U", f->name, d.len, off);
		return 1;
	}
	dbglog1(f->trk, "%s: written %L @%U", f->name, d.len, off);
	if (off + d.len > f->size)
		f->size = off + d.len;
	return 0;
}

static int fo_process(void *ctx, fmed_track_info *ti)
{
	struct fo *f = ctx;

	uint64 off = f->off_cur;
	if ((int64)ti->output.seek != FMED_NULL) {
		off = ti->output.seek;
		ti->output.seek = FMED_NULL;
		dbglog1(ti->trk, "%s: seek @%U", f->name, off);
	}

	ffstr in = ti->data_in;
	for (;;) {
		ffstr d;
		ffsize n = in.len;
		int64 woff = fbuf_write(&f->wbuf, BUF_SIZE, &in, off, &d);
		off += n - in.len;
		if (n != in.len) {
			dbglog1(ti->trk, "%s: write: bufferred %L bytes @%U+%L"
				, f->name, n - in.len, f->wbuf.off, f->wbuf.len);
		}

		if (woff < 0) {
			if (ti->flags & FMED_FFIRST) {
				ffstr d;
				ffstr_set(&d, f->wbuf.ptr, f->wbuf.len);
				if (d.len != 0 && 0 != fo_write(f, d, f->wbuf.off))
					return FMED_RERR;
				f->fin = 1;
				return FMED_RDONE;
			}
			break;
		}

		if (0 != fo_write(f, d, woff))
			return FMED_RERR;

		f->wbuf.len = 0;
		f->wbuf.off = 0;
	}

	f->off_cur = off;
	return FMED_RMORE;
}

const fmed_filter file_output = {
	fo_open, fo_process, fo_close
};

#undef BUF_SIZE
#undef ALIGN
