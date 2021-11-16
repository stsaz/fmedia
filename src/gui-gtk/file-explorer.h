/** fmedia: GUI: file explorer tab
2021, Simon Zolin */

#include <FFOS/dirscan.h>

static struct exp_file* _exp_names_idx(ffsize idx);

static int _exp_conf_path(ffparser_schem *ps, void *obj, char *val)
{
	struct gui_wmain *w = obj;
	ffstr_setz(&w->exp_path, val);
	return 0;
}
static const ffpars_arg _exp_conf[] = {
	{ "disable",	FFPARS_TBOOL8, FFPARS_DSTOFF(struct gui_wmain, exp_disable) },
	{ "path",	FFPARS_TCHARPTR | FFPARS_FSTRZ | FFPARS_FCOPY, FFPARS_DST(_exp_conf_path) },
};
int wmain_exp_conf(ffparser_schem *p, void *obj, ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, gg->wmain, _exp_conf, FFCNT(_exp_conf));
	return 0;
}

static void _exp_conf_writeval(ffconfw *conf, ffuint i)
{
	struct gui_wmain *w = gg->wmain;
	switch (i) {
	case 0:
		ffconfw_addint(conf, w->exp_disable);
		break;
	case 1:
		ffconfw_addstr(conf, &w->exp_path);
		break;
	}
}

int wmain_exp_conf_writeval(ffstr *line, ffconfw *conf)
{
	struct gui_wmain *w = gg->wmain;
	static const char setts[][24+1] = {
		"gui.gui.explorer.disable",
		"gui.gui.explorer.path",
	};

	FF_ASSERT(FF_COUNT(w->exp_conf_flags) == FF_COUNT(setts));

	if (line == NULL) {
		for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
			if (!w->exp_conf_flags[i]) {
				ffconfw_addkeyz(conf, setts[i]);
				_exp_conf_writeval(conf, i);
			}
		}
		return 0;
	}

	for (ffuint i = 0;  i != FF_COUNT(setts);  i++) {
		if (ffstr_matchz(line, setts[i])) {
			ffconfw_addkeyz(conf, setts[i]);
			_exp_conf_writeval(conf, i);
			w->exp_conf_flags[i] = 1;
			return 1;
		}
	}
	return 0;
}

struct exp_file {
	char *name;
	int dir;
};

static void exp_free()
{
	struct gui_wmain *w = gg->wmain;
	struct exp_file *f;
	FFSLICE_WALK(&w->exp_files, f) {
		ffmem_free(f->name);
	}
	ffvec_free(&w->exp_files);
	ffstr_free(&w->exp_path);
}

static void exp_tab_new()
{
	struct gui_wmain *w = gg->wmain;
	if (w->exp_disable) {
		w->exp_tab = -1;
		return;
	}
	char buf[32];
	ffs_format(buf, sizeof(buf), "Explorer%Z");
	ffui_tab_append(&w->tabs, buf);
	w->exp_tab = 0;
}

static int _exp_file_cmp(const void *a, const void *b, void *udata)
{
	const struct exp_file *f1 = (struct exp_file*)a, *f2 = (struct exp_file*)b;
	return ((f1->dir == f2->dir) ? ffsz_cmp(f1->name, f2->name)
		: (f1->dir) ? -1 : 1);
}

static int _exp_names_cmp(const void *a, const void *b, void *udata)
{
	const char **f1 = (const char**)a, **f2 = (const char**)b;
	return ffsz_cmp(*f1, *f2);
}

/** Return 1 if file extension is supported */
static int _exp_file_supported(const char *name)
{
	ffstr ext = {};
	ffpath_splitname(name, ffsz_len(name), NULL, &ext);
	static const char* supp_exts[] = {
		"aac",
		"ape",
		"avi",
		"caf",
		"cue",
		"flac",
		"m3u",
		"m3u8",
		"m4a",
		"mka",
		"mkv",
		"mp3",
		"mp4",
		"mpc",
		"ogg",
		"opus",
		"pls",
		"wav",
		"wv",
	};
	if (ffszarr_ifindsorted(supp_exts, FF_COUNT(supp_exts), ext.ptr, ext.len) < 0)
		return 0;
	return 1;
}

/** Add file names to an array */
static void _exp_add(ffdirscan *d, ffvec *v, const char *path)
{
	char *fname = NULL;
	for (;;) {
		const char *name = ffdirscan_next(d);
		if (name == NULL)
			break;

		struct exp_file *f = ffvec_pushT(v, struct exp_file);
		f->dir = 0;

		ffmem_free(fname);
		fname = ffsz_allocfmt("%s%s", path, name);
		fffileinfo fi;
		if (0 == fffile_info_path(fname, &fi))
			f->dir = fffile_isdir(fffileinfo_attr(&fi));

		if (!f->dir && !_exp_file_supported(name)) {
			v->len--;
			continue;
		}

		f->name = ffsz_dup(name);
	}
	ffslice_sortT((ffslice*)v, _exp_file_cmp, NULL, struct exp_file);
	ffmem_free(fname);
}

/** Prepare file listing */
static int _exp_list_fill(const char *path)
{
	struct gui_wmain *w = gg->wmain;

	ffdirscan d = {};
	if (0 != ffdirscan_open(&d, path, FFDIRSCAN_NOSORT)) {
		syserrlog("ffdirscan_open: %s", path);
		return -1;
	}

	struct exp_file *f;
	FFSLICE_WALK(&w->exp_files, f) {
		ffmem_free(f->name);
	}
	w->exp_files.len = 0;

	f = ffvec_pushT(&w->exp_files, struct exp_file);
	f->dir = 1;
	f->name = "";
	_exp_add(&d, &w->exp_files, path);
	ffdirscan_close(&d);

	f = (struct exp_file*)w->exp_files.ptr;
	f->name = ffsz_dup("<UP>");
	return 0;
}

/** Recursively add file names to an array */
static void _exp_add_r(ffvec *v, const char *path)
{
	ffdirscan d = {};
	if (0 != ffdirscan_open(&d, path, FFDIRSCAN_NOSORT)) {
		syserrlog("ffdirscan_open: %s", path);
		return;
	}

	char *fname = NULL;
	for (;;) {
		const char *name = ffdirscan_next(&d);
		if (name == NULL)
			break;

		ffmem_free(fname);
		fname = ffsz_allocfmt("%s/%s", path, name);
		fffileinfo fi;
		int dir = 0;
		if (0 == fffile_info_path(fname, &fi))
			dir = fffile_isdir(fffileinfo_attr(&fi));

		if (dir) {
			_exp_add_r(v, fname);
		} else {
			if (!_exp_file_supported(name))
				continue;
			*ffvec_pushT(v, char*) = fname;
			fname = NULL;
		}
	}
	ffmem_free(fname);
	ffdirscan_close(&d);
}

/** Prepare an array of full file names to add to the current playlist */
static void _exp_addall(ffslice sel, int play_idx)
{
	struct gui_wmain *w = gg->wmain;
	ffvec v = {};
	uint *it;
	FFSLICE_WALK(&sel, it) {
		struct exp_file *f = _exp_names_idx(*it);
		if (f == NULL)
			continue;
		char *newpath = ffsz_allocfmt("%S%s", &w->exp_path, f->name);
		if (f->dir)
			_exp_add_r(&v, newpath);
		else
			*ffvec_pushT(&v, char*) = ffsz_dup(newpath);
		ffmem_free(newpath);
	}

	ffslice_sortT((ffslice*)&v, _exp_names_cmp, NULL, char*);

	struct params_urls_add_play *p = ffmem_new(struct params_urls_add_play);
	p->play = play_idx;
	p->v = v;
	corecmd_add(_A_URLS_ADD_PLAY, p);
}

static void _exp_list_setdata(void *param)
{
	struct gui_wmain *w = gg->wmain;
	ffui_view_setdata(&w->vlist, 0, w->exp_files.len);
}

/** Handle double-click event */
static void exp_list_action(int idx)
{
	struct gui_wmain *w = gg->wmain;
	struct exp_file *f = _exp_names_idx(idx);
	if (f == NULL)
		return;

	if (!f->dir) {
		struct params_urls_add_play *p = ffmem_new(struct params_urls_add_play);
		struct exp_file *it;
		FFSLICE_WALK(&w->exp_files, it) {
			if (!it->dir) {
				if (f == it)
					p->play = p->v.len;
				*ffvec_pushT(&p->v, char*) = ffsz_allocfmt("%s%s", w->exp_path.ptr, it->name);
			}
		}
		corecmd_add(_A_URLS_ADD_PLAY, p);
		return;
	}

	char *newpath;
	if (idx == 0) {
		newpath = ffsz_allocfmt("%S..", &w->exp_path);
		ffssize n = ffpath_normalize(newpath, -1, newpath, ffsz_len(newpath), 0);
		newpath[n] = '\0';
	} else {
		newpath = ffsz_allocfmt("%S%s/", &w->exp_path, f->name);
	}
	if (0 != _exp_list_fill(newpath))
		goto done;

	ffstr_free(&w->exp_path);
	ffstr_setz(&w->exp_path, newpath);
	newpath = NULL;

	ffui_view_clear(&w->vlist);
	ffui_thd_post(_exp_list_setdata, 0, 0);
done:
	ffmem_free(newpath);
}

/** Fill the contents for the first time and redraw the list */
static void exp_list_show()
{
	struct gui_wmain *w = gg->wmain;
	ffui_view_popupmenu(&w->vlist, &gg->mexplorer);
	if (w->exp_files.len == 0) {
		if (w->exp_path.len == 0) {
			w->exp_path.ptr = core->env_expand(NULL, 0, "$HOME/");
			if (w->exp_path.ptr == NULL)
				w->exp_path.ptr = ffsz_dup("/");
			w->exp_path.len = ffsz_len(w->exp_path.ptr);
		}
		_exp_list_fill(w->exp_path.ptr);
	}
	ffui_thd_post(_exp_list_setdata, 0, 0);
}

static struct exp_file* _exp_names_idx(ffsize idx)
{
	struct gui_wmain *w = gg->wmain;
	if (idx >= w->exp_files.len)
		return NULL;
	struct exp_file *f = ffslice_itemT(&w->exp_files, idx, struct exp_file);
	return f;
}

static void exp_list_dispinfo(struct ffui_view_disp *disp)
{
	// struct gui_wmain *w = gg->wmain;

	switch (disp->sub) {
	case H_DUR:
	case H_TIT: {
		struct exp_file *f = _exp_names_idx(disp->idx);
		if (f == NULL)
			break;

		if (disp->sub == H_DUR) {
			if (!f->dir)
				break;
			disp->text.len = _ffs_copyz(disp->text.ptr, disp->text.len, "<DIR>");
			return;
		}

		ffstr val = FFSTR_INITZ(f->name);
		disp->text.len = _ffs_copy(disp->text.ptr, disp->text.len, val.ptr, val.len);
		return;
	}
	}

	disp->text.len = 0;
}

static void exp_action(int id)
{
	struct gui_wmain *w = gg->wmain;
	if (ffui_tab_active(&w->tabs) != w->exp_tab)
		return;
	ffarr4 *sel = ffui_view_getsel(&w->vlist);

	switch (id) {
	case A_EXPL_ADDPLAY:
		_exp_addall(*(ffslice*)sel, 0);
		break;

	case A_EXPL_ADD:
		_exp_addall(*(ffslice*)sel, -1);
		break;
	}

	ffui_view_sel_free(sel);
}
