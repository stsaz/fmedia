/** fmedia/Android: file-read filter
2022, Simon Zolin */

#include <FFOS/file.h>

struct fi {
	fffd fd;
	ffvec buf;
};

void fi_close(void *ctx)
{
	struct fi *f = ctx;
	ffvec_free(&f->buf);
	fffile_close(f->fd);
	ffmem_free(f);
}

void* fi_open(fmed_track_info *ti)
{
	struct fi *f = ffmem_new(struct fi);
	ffvec_alloc(&f->buf, 64*1024, 1);

	if (FFFILE_NULL == (f->fd = fffile_open(ti->in_filename, FFFILE_READONLY))) {
		syserrlog1(ti->trk, "fffile_open: %s", ti->in_filename);
		goto end;
	}

	fffileinfo fi;
	if (0 != fffile_info(f->fd, &fi)) {
		syserrlog1(ti->trk, "fffile_info: %s", ti->in_filename);
		goto end;
	}
	ti->input.size = fffile_infosize(&fi);

	dbglog1(ti->trk, "opened %s (%U kbytes)"
		, ti->in_filename, ti->input.size / 1024);
	return f;

end:
	fi_close(f);
	return NULL;
}

int fi_process(void *ctx, fmed_track_info *ti)
{
	struct fi *f = ctx;

	if ((int64)ti->input.seek != FMED_NULL) {
		fffile_seek(f->fd, ti->input.seek, FFFILE_SEEK_BEGIN);
		dbglog1(ti->trk, "file seek: %xU", ti->input.seek);
		ti->input.seek = FMED_NULL;
	}

	int r = fffile_read(f->fd, f->buf.ptr, f->buf.cap);
	if (r < 0)
		return FMED_RERR;
	else if (r == 0)
		return FMED_RDONE;
	dbglog1(ti->trk, "file read: %L", r);
	f->buf.len = r;
	ffstr_set(&ti->data_out, f->buf.ptr, r);
	return FMED_RDATA;
}

const fmed_filter file_input = {
	fi_open, fi_process, fi_close
};
