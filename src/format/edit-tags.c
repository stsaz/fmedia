/** fmedia: modify file tags in-place
2022, Simon Zolin */

#include <fmedia.h>
#include <format/mmtag.h>
#include <util/svar.h>
#include <avpack/id3v1.h>
#include <avpack/id3v2.h>
#include <FFOS/path.h>

extern const fmed_core *core;
#undef syserrlog
#undef errlog
#undef dbglog
#undef infolog
#define syserrlog(...)  fmed_syserrlog(core, NULL, "edittags", __VA_ARGS__)
#define errlog(...)  fmed_errlog(core, NULL, "edittags", __VA_ARGS__)
#define dbglog(...)  fmed_dbglog(core, NULL, "edittags", __VA_ARGS__)
#define infolog(...)  fmed_infolog(core, NULL, "edittags", __VA_ARGS__)

extern const char file_ext[][5];
extern int file_format_detect(const void *data, ffsize len);

struct edittags {
	struct fmed_edittags_conf conf;
	fffd fd, fdw;
	ffvec buf;
	ffvec meta2;
	int meta_clear;
	fftime mtime;
	char *fnw;
	int done;
};

void edittags_close(struct edittags *c)
{
	if (c->done && c->conf.preserve_date) {
		fffile_set_mtime(c->fdw, &c->mtime);
	}
	fffile_close(c->fd);
	if (c->fd != c->fdw) {
		fffile_close(c->fdw);
		if (c->done && 0 != fffile_rename(c->fnw, c->conf.fn)) {
			syserrlog("file rename: %s -> %s", c->fnw, c->conf.fn);
			c->done = 0;
		}
	}
	if (c->done) {
		infolog("saved file %s", c->conf.fn);
	}
	ffmem_free(c->fnw);
	ffvec_free(&c->buf);
	ffvec_free(&c->meta2);
}

int file_copydata(fffd src, ffuint64 offsrc, fffd dst, ffuint64 offdst, ffuint64 size)
{
	int rc = -1, r;
	ffvec v = {};
	ffvec_alloc(&v, 64*1024, 1);

	while (size != 0) {
		ffuint n = ffmin(size, v.cap);
		if (0 > (r = fffile_readat(src, v.ptr, n, offsrc)))
			goto end;
		if (0 > (r = fffile_writeat(dst, v.ptr, n, offdst)))
			goto end;
		offsrc += n;
		offdst += n;
		size -= n;
	}

	rc = 0;

end:
	ffvec_free(&v);
	return rc;
}

/**
Return >0: tag
 =0: done
 <0: error */
int meta_next(ffstr *m, ffstr *k, ffstr *v)
{
	if (m->len == 0)
		return 0;

	ffstr kv;
	for (;;) {
		ffstr_splitby(m, ';', &kv, m);
		if (kv.len == 0)
			continue;
		if (0 > ffstr_splitby(&kv, '=', k, v)) {
			errlog("invalid --meta: %S", &kv);
			return -1;
		}
		break;
	}

	int tag;
	if (-1 == (tag = ffs_findarrz(ffmmtag_str, FF_COUNT(ffmmtag_str), k->ptr, k->len)))
		return -1;
	return tag;
}

int mp3_id3v2(struct edittags *c)
{
	int r = FMED_RERR;
	uint id3v2_size = 0;
	struct id3v2write w = {};
	id3v2write_create(&w);
	struct id3v2read id3v2 = {};
	id3v2read_open(&id3v2);

	if (0 > (r = fffile_readat(c->fd, c->buf.ptr, c->buf.cap, 0))) {
		syserrlog("file read");
		return FMED_RERR;
	}
	c->buf.len = r;

	ffstr m, in, k, v, k2, v2;

	m = c->conf.meta;
	for (;;) {
		int tag = meta_next(&m, &k, &v);
		if (tag == 0)
			break;
		else if (tag < 0)
			goto end;
		dbglog("id3v2: writing %S = %S", &k, &v);
		id3v2write_add(&w, tag, v);
	}

	ffstr_setstr(&in, &c->buf);
	r = id3v2read_process(&id3v2, &in, &k2, &v2);
	if (r != ID3V2READ_NO)
		id3v2_size = id3v2read_size(&id3v2);

	if (!c->meta_clear) {
		// copy existing tags
		while (r <= 0) {
			m = c->conf.meta;
			for (;;) {
				int tag = meta_next(&m, &k, &v);
				if (tag == 0) {
					dbglog("id3v2: writing %S = %S", &k2, &v2);
					id3v2write_add(&w, -r, v2);
					break;
				}
				if (tag == -r)
					break;
			}

			r = id3v2read_process(&id3v2, &in, &k2, &v2);
		}
	}

	int padding = 1000;
	if (id3v2_size >= w.buf.len)
		padding = id3v2_size - w.buf.len;
	if (0 != (r = id3v2write_finish(&w, padding))) {
		errlog("id3v2write_finish");
		goto end;
	}
	ffvec_free(&c->buf);
	c->buf = w.buf;
	ffvec_null(&w.buf);

	dbglog("id3v2: old size: %u, new size: %L", id3v2_size, c->buf.len);

	if (id3v2_size >= c->buf.len) {
		if (0 > (r = fffile_writeat(c->fd, c->buf.ptr, c->buf.len, 0))) {
			syserrlog("file write");
			goto end;
		}

	} else if (id3v2_size < c->buf.len) {
		c->fnw = ffsz_allocfmt("%s.fmediatemp", c->conf.fn);
		if (FFFILE_NULL == (c->fdw = fffile_open(c->fnw, FFFILE_CREATENEW | FFFILE_WRITEONLY))) {
			syserrlog("file create: %s", c->fnw);
			goto end;
		}
		dbglog("created file: %s", c->fnw);

		if (0 > (r = fffile_writeat(c->fdw, c->buf.ptr, c->buf.len, 0))) {
			syserrlog("file write");
			goto end;
		}

		ffint64 sz = fffile_size(c->fd);
		if (0 != file_copydata(c->fd, id3v2_size, c->fdw, c->buf.len, sz)) {
			syserrlog("file read/write");
			goto end;
		}
	}

	r = FMED_RDONE;

end:
	id3v2read_close(&id3v2);
	id3v2write_close(&w);
	return r;
}

int mp3_id3v1(struct edittags *c)
{
	int r, have_id3v1 = 0;
	ffint64 sz = fffile_size(c->fd);
	ffint64 id3v1_off = sz - sizeof(struct id3v1);
	id3v1_off = ffmax(id3v1_off, 0);
	if (id3v1_off > 0) {
		if (0 > (r = fffile_readat(c->fd, c->buf.ptr, sizeof(struct id3v1), id3v1_off))) {
			syserrlog("file read");
			return FMED_RERR;
		}
		c->buf.len = r;
	}

	struct id3v1 w = {};
	id3v1write_init(&w);

	ffstr m, k, v, v2;

	m = c->conf.meta;
	for (;;) {
		int tag = meta_next(&m, &k, &v);
		if (tag == 0)
			break;
		else if (tag < 0)
			return FMED_RERR;
		r = id3v1write_set(&w, tag, v);
		if (r != 0)
			dbglog("id3v1: written %S = %S", &k, &v);
	}

	struct id3v1read rd = {};
	rd.codepage = core->props->codepage;
	ffstr id31_data = *(ffstr*)&c->buf;
	r = id3v1read_process(&rd, id31_data, &v2);
	if (r != ID3V1READ_NO)
		have_id3v1 = 1;

	if (!c->meta_clear) {
		// copy existing tags
		while (r < 0) {
			m = c->conf.meta;
			for (;;) {
				int tag = meta_next(&m, &k, &v);
				if (tag == 0) {
					int r2 = id3v1write_set(&w, -r, v2);
					if (r2 != 0)
						dbglog("id3v1: written %s = %S", ffmmtag_str[-r], &v2);
					break;
				}
				if (tag == -r)
					break;
			}

			r = id3v1read_process(&rd, id31_data, &v2);
		}
	}

	id3v1_off = fffile_size(c->fdw);
	if (have_id3v1)
		id3v1_off -= sizeof(struct id3v1);
	if (0 > (r = fffile_writeat(c->fdw, &w, sizeof(struct id3v1), id3v1_off))) {
		syserrlog("file write");
		return FMED_RERR;
	}
	return FMED_RDONE;
}

int add_meta_from_filename(struct edittags *c)
{
	int rc = -1;
	if (c->conf.meta_from_filename.len == 0)
		return 0;

	ffstr fn = FFSTR_INITZ(c->conf.fn);
	ffpath_splitpath(fn.ptr, fn.len, NULL, &fn);
	ffpath_splitname(fn.ptr, fn.len, &fn, NULL);

	ffvec fmt = {}, vars = {};
	ffstr m = c->conf.meta_from_filename, val;
	while (m.len != 0) {
		int r = svar_split(&m, &val);
		if (r == FFSVAR_S) {
			*ffvec_pushT(&vars, ffstr) = val;
			ffvec_addsz(&fmt, "%S");
		} else {
			ffvec_addstr(&fmt, &val);
		}
	}
	ffvec_addchar(&fmt, '\0');

	if (vars.len > 10) {
		errlog("too many variables in template: %S", &c->conf.meta_from_filename);
		goto end;
	}

	ffstr args[10];
	if (0 != ffstr_matchfmt(&fn, fmt.ptr, &args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6], &args[7], &args[8], &args[9])) {
		errlog("file name '%S' doesn't match template '%S'", &fn, &c->conf.meta_from_filename);
		goto end;
	}

	ffvec_addstr(&c->meta2, &c->conf.meta);

	ffstr *it;
	uint i = 0;
	FFSLICE_WALK(&vars, it) {
		ffvec_addfmt(&c->meta2, ";%S=%S", it, &args[i]);
		i++;
	}
	ffstr_setstr(&c->conf.meta, &c->meta2);
	dbglog("meta after processing file name: %S", &c->conf.meta);
	rc = 0;

end:
	ffvec_free(&fmt);
	ffvec_free(&vars);
	return rc;
}

int edittags_process(struct edittags *c)
{
	int r;

	ffstr m, kv;
	ffstr_splitby(&c->conf.meta, ';', &kv, &m);
	if (ffstr_eqz(&kv, "clear")) {
		c->meta_clear = 1;
		c->conf.meta = m;
	}

	if (0 != add_meta_from_filename(c)) {
		return FMED_RERR;
	}

	if (FFFILE_NULL == (c->fd = fffile_open(c->conf.fn, FFFILE_READWRITE))) {
		syserrlog("file open: %s", c->conf.fn);
		return FMED_RERR;
	}
	c->fdw = c->fd;

	fffileinfo fi = {};
	if (0 != fffile_info(c->fd, &fi)){
		syserrlog("file info: %s", c->conf.fn);
		return FMED_RERR;
	}
	c->mtime = fffileinfo_mtime(&fi);

	ffvec_alloc(&c->buf, 1024, 1);
	if (0 > (r = fffile_read(c->fd, c->buf.ptr, c->buf.cap))) {
		syserrlog("file read");
		return FMED_RERR;
	}
	c->buf.len = r;

	uint fmt = file_format_detect(c->buf.ptr, c->buf.len);
	if (fmt == 0) {
		errlog("can't detect file format");
		return FMED_RERR;
	}

	const char *ext = file_ext[fmt];
	if (ffsz_eq(ext, "mp3")) {
		if (FMED_RDONE == (r = mp3_id3v2(c)))
			r = mp3_id3v1(c);
	} else {
		errlog("unsupported format");
	}
	c->done = (r == FMED_RDONE);
	return r;
}

void edittags_edit(struct fmed_edittags_conf *conf)
{
	struct edittags et = {};
	et.fd = FFFILE_NULL;
	et.fdw = FFFILE_NULL;
	et.conf = *conf;
	edittags_process(&et);
	edittags_close(&et);
}

const fmed_edittags edittags_filt = {
	edittags_edit
};
