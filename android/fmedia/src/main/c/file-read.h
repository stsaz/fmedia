/** fmedia/Android: file-read filter
2022, Simon Zolin */

#include <util/fcache.h>
#include <FFOS/file.h>

#define ALIGN (4*1024)
#define BUF_SIZE (64*1024)

struct fi {
	fffd fd;
	uint64 off_cur;
	struct fcache fcache;
};

static void fi_close(void *ctx)
{
	struct fi *f = ctx;
	fcache_destroy(&f->fcache);
	fffile_close(f->fd);
	ffmem_free(f);
}

static void* fi_open(fmed_track_info *ti)
{
	struct fi *f = ffmem_new(struct fi);
	f->fd = FFFILE_NULL;
	if (0 != fcache_init(&f->fcache, 2, BUF_SIZE, ALIGN))
		goto end;

	if (FFFILE_NULL == (f->fd = fffile_open(ti->in_filename, FFFILE_READONLY))) {
		syserrlog1(ti->trk, "fffile_open: %s", ti->in_filename);
		if (fferr_notexist(fferr_last()))
			ti->error = FMED_E_NOSRC;
		goto end;
	}

	fffileinfo fi;
	if (0 != fffile_info(f->fd, &fi)) {
		syserrlog1(ti->trk, "fffile_info: %s", ti->in_filename);
		goto end;
	}
	ti->input.size = fffileinfo_size(&fi);
	ti->in_mtime = fffileinfo_mtime(&fi);
	ti->in_mtime.sec += FFTIME_1970_SECONDS;

	dbglog1(ti->trk, "%s: opened (%U kbytes)"
		, ti->in_filename, ti->input.size / 1024);
	return f;

end:
	fi_close(f);
	return NULL;
}

static int fi_process(void *ctx, fmed_track_info *ti)
{
	struct fi *f = ctx;

	if ((int64)ti->input.seek != FMED_NULL) {
		f->off_cur = ti->input.seek;
		ti->input.seek = FMED_NULL;
		dbglog1(ti->trk, "%s: seek @%U", ti->in_filename, f->off_cur);
	}
	uint64 off = f->off_cur;

	struct fcache_buf *b;
	if (NULL != (b = fcache_find(&f->fcache, off))) {
		dbglog1(ti->trk, "%s: cache hit: %L @%U", ti->in_filename, b->len, b->off);
		goto done;
	}

	b = fcache_nextbuf(&f->fcache);

	ffuint64 off_al = ffint_align_floor2(off, ALIGN);
	ffssize r = fffile_readat(f->fd, b->ptr, BUF_SIZE, off_al);
	if (r < 0) {
		syserrlog1(ti->trk, "%s: read: %s", ti->in_filename);
		return FMED_RERR;
	}
	b->len = r;
	b->off = off_al;
	dbglog1(ti->trk, "%s: read: %L @%U", ti->in_filename, b->len, b->off);

done:
	f->off_cur = b->off + b->len;
	ffstr_set(&ti->data_out, b->ptr, b->len);
	ffstr_shift(&ti->data_out, off - b->off);
	if (ti->data_out.len == 0)
		return FMED_RDONE;
	return FMED_RDATA;
}

const fmed_filter file_input = {
	fi_open, fi_process, fi_close
};

#undef BUF_SIZE
#undef ALIGN
