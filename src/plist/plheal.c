/** fmedia: heal playlist: Auto-correct the paths to files inside a .m3u playlist
2023, Simon Zolin */

#include <fmedia.h>
#include <util/fntree.h>
#include <avpack/m3u.h>
#include <FFOS/dirscan.h>
#include <ffbase/map.h>

extern fmed_core *core;
#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define infolog1(trk, ...)  fmed_infolog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

struct plheal {
	fmed_track_info *ti;
	m3uread mr;
	ffvec pl_dir;
	ffstr in;
	m3uwrite mw;
	m3uwrite_entry me;
	ffvec url, artist, title;
	ffvec buf;
	ffmap map; // "filename" -> struct plh_map_ent{ "/path/filename.ext" }
	uint nfixed, total;

	ffdirscan ds;
	ffvec ds_path;

	uint success :1;
};

static void plh_free_table(struct plheal *p);

static void plheal_close(void *ctx)
{
	struct plheal *p = ctx;
	m3uread_close(&p->mr);
	m3uwrite_close(&p->mw);
	ffvec_free(&p->buf);
	ffvec_free(&p->url);
	ffvec_free(&p->artist);
	ffvec_free(&p->title);
	ffvec_free(&p->pl_dir);
	ffvec_free(&p->ds_path);
	ffdirscan_close(&p->ds);
	plh_free_table(p);

	if (p->success) {
		// file.m3u.fmedia -> file.m3u
		if (0 != fffile_rename(p->ti->out_filename, p->ti->in_filename))
			syserrlog1(p->ti->trk, "fffile_rename: %s", p->ti->in_filename);
		else
			dbglog1(p->ti->trk, "renamed: %s", p->ti->in_filename);
		infolog1(p->ti->trk, "Corrected %u/%u paths", p->nfixed, p->total);
	}

	ffmem_free(p);
}

/** Get absolute directory for the input playlist file */
static int abs_dir(const char *fn, ffvec *buf, fmed_track_info *ti)
{
	ffstr s = FFSTR_INITZ(fn);
	ffstr dir;
	ffpath_splitpath_str(s, &dir, NULL);
	ffvec_set2(buf, &dir);
	if (!ffpath_abs(dir.ptr, dir.len)) {
		ffvec_alloc(buf, 4*1024, 1);
		if (0 != ffps_curdir(buf->ptr, buf->cap)) {
			syserrlog1(ti->trk, "ffps_curdir");
			return -1;
		}
		buf->len = ffsz_len(buf->ptr);
		ffvec_addfmt(buf, "/%S", &dir);
		int r = ffpath_normalize(buf->ptr, buf->cap, buf->ptr, buf->len, 0);
		FF_ASSERT(r >= 0);
		buf->len = r;
	}
	return 0;
}

static void* plheal_open(fmed_track_info *ti)
{
	struct plheal *p = ffmem_new(struct plheal);
	p->ti = ti;
	ti->out_filename = ffsz_allocfmt("%s.fmedia", ti->in_filename);

	if (0 != abs_dir(ti->in_filename, &p->pl_dir, ti)) {
		plheal_close(p);
		return NULL;
	}

	m3uread_open(&p->mr);
	m3uwrite_create(&p->mw, 0);
	return p;
}

/** Return TRUE if 'parent' is a parent directory of 'file'.
Both paths must be normalized. */
static inline int path_isparent(ffstr parent, ffstr file)
{
	return (file.len > parent.len && ffpath_slash(file.ptr[parent.len])
		&& ffstr_match2(&file, &parent));
}

/** Find an existing file with the same name but different extension.
/path/dir/file.mp3 -> /path/dir/file.m4a */
static int plh_fix_ext(struct plheal *p, ffstr fn, ffvec *output)
{
	int rc = -1;

	ffstr dir, name;
	ffpath_splitpath_str(fn, &dir, &name);
	ffpath_splitname_str(name, &name, NULL);

	if (!ffstr_eq2(&dir, &p->ds_path)) {
		p->ds_path.len = 0;
		ffvec_addfmt(&p->ds_path, "%S%Z", &dir);
		p->ds_path.len--;
		ffdirscan_close(&p->ds);
		ffmem_zero_obj(&p->ds);
		if (0 != ffdirscan_open(&p->ds, p->ds_path.ptr, 0))
			goto end;
	} else {
		dbglog1(p->ti->trk, "using ffdirscan object from cache");
		ffdirscan_reset(&p->ds);
	}

	const char *s;
	while (NULL != (s = ffdirscan_next(&p->ds))) {
		ffstr ss = FFSTR_INITZ(s);
		if (ffstr_match2(&ss, &name) && s[name.len] == '.')
			break;
	}
	if (s == NULL)
		goto end;

	output->len = 0;
	ffvec_addfmt(output, "%S/%s", &dir, s);
	rc = 0;

end:
	if (rc != 0) {
		dbglog1(p->ti->trk, "%S: couldn't find similar file in '%S'"
			, &fn, &dir);
		output->len = 0;
	}
	return rc;
}

struct plh_map_ent {
	ffstr name, path;
};

static int plh_map_keyeq_func(void *opaque, const void *key, ffsize keylen, void *val)
{
	struct plh_map_ent *me = val;
	return ffstr_eq(&me->name, key, keylen);
}

/** Create a table containing all file paths inside the playlist's directory */
static void plh_create_table(struct plheal *p)
{
	ffdirscan ds = {};
	fntree_block *root = NULL;
	fffd f = FFFILE_NULL;
	char *fpath = NULL;
	char *pl_dirz = ffsz_dupstr((ffstr*)&p->pl_dir);

	ffmap_init(&p->map, plh_map_keyeq_func);

	dbglog1(p->ti->trk, "scanning %s", pl_dirz);
	if (0 != ffdirscan_open(&ds, pl_dirz, 0))
		goto end;

	ffstr path = FFSTR_INITZ(pl_dirz);
	if (NULL == (root = fntree_from_dirscan(path, &ds, 0)))
		goto end;
	ffdirscan_close(&ds);

	fntree_block *blk = root;
	fntree_cursor cur = {};
	for (;;) {
		fntree_entry *e;
		if (NULL == (e = fntree_cur_next_r_ctx(&cur, &blk)))
			break;

		ffstr path = fntree_path(blk);
		ffstr name = fntree_name(e);
		ffmem_free(fpath);
		fpath = ffsz_allocfmt("%S%c%S", &path, FFPATH_SLASH, &name);

		fffile_close(f);
		if (FFFILE_NULL == (f = fffile_open(fpath, FFFILE_READONLY)))
			continue;

		fffileinfo fi;
		if (0 != fffile_info(f, &fi))
			continue;

		if (fffile_isdir(fffileinfo_attr(&fi))) {

			ffmem_zero_obj(&ds);

			uint dsflags = 0;
#ifdef FF_LINUX
			dsflags = FFDIRSCAN_USEFD;
			ds.fd = f;
			f = FFFILE_NULL;
#endif

			dbglog1(p->ti->trk, "scanning %s", path.ptr);
			if (0 != ffdirscan_open(&ds, path.ptr, dsflags))
				continue;

			ffstr_setz(&path, fpath);
			if (NULL == (blk = fntree_from_dirscan(path, &ds, 0)))
				continue;
			ffdirscan_close(&ds);

			fntree_attach(e, blk);
			continue;
		}

		struct plh_map_ent *me = ffmem_new(struct plh_map_ent);
		ffstr_setz(&me->path, fpath);
		fpath = NULL;
		ffpath_splitpath_str(me->path, NULL, &me->name);
		ffpath_splitname_str(me->name, &me->name, NULL);
		ffmap_add(&p->map, me->name.ptr, me->name.len, me);
	}

end:
	ffmem_free(pl_dirz);
	ffmem_free(fpath);
	fffile_close(f);
	ffdirscan_close(&ds);
	fntree_free_all(root);
}

static void plh_free_table(struct plheal *p)
{
	struct _ffmap_item *it;
	FFMAP_WALK(&p->map, it) {
		if (!_ffmap_item_occupied(it))
			continue;
		struct plh_map_ent *me = it->val;
		ffmem_free(me->path.ptr);
	}
	ffmap_free(&p->map);
}

/** Find an existing file with the same name (and probably different extension)
 recursively in playlist's directory.
/path/dir/olddir/file.mp3 -> /path/dir/newdir/file.m4a */
static int plh_fix_dir(struct plheal *p, ffstr fn, ffvec *output)
{
	if (p->map.len == 0)
		plh_create_table(p);

	ffstr dir, name;
	ffpath_splitpath_str(fn, &dir, &name);
	ffpath_splitname_str(name, &name, NULL);

	const struct plh_map_ent *me = ffmap_find(&p->map, name.ptr, name.len, NULL);
	if (me == NULL)
		return -1;

	ffvec_addstr(output, &me->path);
	return 0;
}

/** Return absolute & normalized file path */
static char* plh_abs_norm(struct plheal *p, ffvec *buf, ffstr path)
{
	buf->len = 0;

	if (!ffpath_abs(path.ptr, path.len)) {
		ffvec_addfmt(buf, "%S/%S", &p->pl_dir, &path);
		path = *(ffstr*)buf;
	} else {
		ffvec_realloc(buf, path.len+1, 1);
	}

	int r = ffpath_normalize(buf->ptr, buf->cap, path.ptr, path.len, FFPATH_SLASH_BACKSLASH | FFPATH_FORCE_SLASH);
	FF_ASSERT(r >= 0);
	buf->len = r;
	char *afn = buf->ptr;
	afn[r] = '\0';
	return afn;
}

/** Apply filters to normalize a file name.
Return 0: no change */
static int plh_filepath_heal(struct plheal *p, ffstr path, ffvec *output)
{
	int rc = 0;
	output->len = 0;
	char *afn = plh_abs_norm(p, &p->buf, path);

	if (!fffile_exists(afn)) {
		if (0 != plh_fix_ext(p, *(ffstr*)&p->buf, output)) {
			if (0 != plh_fix_dir(p, *(ffstr*)&p->buf, output)) {
				warnlog1(p->ti->trk, "%s: file doesn't exist and can't be found in %S"
					, afn, &p->pl_dir);
				goto end;
			}
		}
		dbglog1(p->ti->trk, "%s: file doesn't exist but found at %s", afn, output->ptr);
		rc = 1;
	} else {
		ffvec_free(output);
		*output = p->buf;
		ffvec_null(&p->buf);
	}

	if ((rc || ffpath_abs(path.ptr, path.len))
		&& path_isparent(*(ffstr*)&p->pl_dir, *(ffstr*)output)) {

		dbglog1(p->ti->trk, "%S: converting the path to relative"
			, output);
		ffslice_rm((ffslice*)output, 0, p->pl_dir.len + 1, 1);
		rc = 1;
	}

	if (rc)
		dbglog1(p->ti->trk, "%S -> %S", &path, output);

end:
	return rc;
}

/** Parse m3u playlist
Return FMED_RDATA: complete entry */
static int plh_read(struct plheal *p)
{
	for (;;) {
		ffstr out;
		int r = m3uread_process(&p->mr, &p->in, &out);
		switch (r) {
		case M3UREAD_URL:
			p->me.url = out;
			return FMED_RDATA;

		case M3UREAD_ARTIST:
			p->artist.len = 0;
			ffvec_addstr(&p->artist, &out);
			continue;

		case M3UREAD_TITLE:
			p->title.len = 0;
			ffvec_addstr(&p->title, &out);
			continue;

		case M3UREAD_DURATION:
			p->me.duration_sec = m3uread_duration_sec(&p->mr);
			continue;

		case M3UREAD_MORE:
			return FMED_RMORE;

		case M3UREAD_EXT:
			warnlog1(p->ti->trk, "skipping extension line: '%S' @%u"
				, &out, (int)m3uread_line(&p->mr));
			continue;

		case M3UREAD_WARN:
			warnlog1(p->ti->trk, "m3uwrite_process: @%u", (int)m3uread_line(&p->mr));
			continue;

		default:
			FF_ASSERT(0);
			return FMED_RERR;
		}
	}
}

/**
read an entry from input playlist -> filter -> write to output playlist */
static int plheal_process(void *ctx, fmed_track_info *ti)
{
	struct plheal *p = ctx;
	int r;

	if (ti->flags & FMED_FFWD) {
		p->in = ti->data_in;
		ti->data_in.len = 0;
	}

	for (;;) {
		r = plh_read(p);
		switch (r) {
		case FMED_RDATA: break;

		case FMED_RMORE:
			if (ti->flags & FMED_FFIRST) {
				if (p->nfixed == 0)
					return FMED_RFIN;

				ffstr s = m3uwrite_fin(&p->mw);
				ti->data_out = s;
				p->success = 1;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		default: return r;
		}

		dbglog1(ti->trk, "'%S' '%S' - '%S'"
			, &p->me.url, &p->artist, &p->title);

		r = plh_filepath_heal(p, p->me.url, &p->url);
		if (r != 0)
			ffstr_setstr(&p->me.url, &p->url);
		if (r != 0)
			p->nfixed++;
		p->total++;

		ffstr_setstr(&p->me.artist, &p->artist);
		ffstr_setstr(&p->me.title, &p->title);
		r = m3uwrite_process(&p->mw, &p->me);
		if (r != 0) {
			errlog1(ti->trk, "m3uwrite_process");
			return FMED_RERR;
		}
		ffmem_zero_obj(&p->me);
		continue;
	}
}

const fmed_filter fmed_plheal = { plheal_open, plheal_process, plheal_close };
