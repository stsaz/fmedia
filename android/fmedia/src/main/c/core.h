/** fmedia/Android: core
2022, Simon Zolin */

#include <ffbase/map.h>

static fmed_core _core;

struct filter_pair {
	char *ext;
	const fmed_filter *iface;
};

struct core_ctx {
	fmed_props props;
	ffmap ext_filter; // char[] -> struct filter_pair*
};
static struct core_ctx *cx;

static int ext_filter_keyeq(void *opaque, const void *key, ffsize keylen, void *val)
{
	const struct filter_pair *fp = val;
	ffstr s = FFSTR_INITN(key, keylen);
	return ffstr_ieqz(&s, fp->ext);
}

extern const fmed_filter mp3_input;
extern const fmed_filter mp4_input;

static void init_filters()
{
	ffmap_init(&cx->ext_filter, ext_filter_keyeq);
	static const struct filter_pair filters[] = {
		{ "m4a", &mp4_input },
		{ "mp3", &mp3_input },
		{ "mp4", &mp4_input },
	};
	for (uint i = 0;  i != FF_COUNT(filters);  i++) {
		ffmap_add(&cx->ext_filter, filters[i].ext, ffsz_len(filters[i].ext), (void*)&filters[i]);
	}
}

int core_init()
{
	cx = ffmem_new(struct core_ctx);
	cx->props.codepage = FFUNICODE_WIN1252;
	_core.props = &cx->props;
	core = &_core;
	init_filters();

	// ffmap_free(&cx->ext_filter);
	// ffmem_free(cx);
	return 0;
}

const fmed_filter* core_filter(ffstr ext)
{
	const struct filter_pair *f = ffmap_find(&cx->ext_filter, ext.ptr, ext.len, NULL);
	if (f == NULL)
		return NULL;
	return f->iface;
}

static fmed_core _core = {
};
