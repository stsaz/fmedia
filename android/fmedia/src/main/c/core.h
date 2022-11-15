/** fmedia/Android: core
2022, Simon Zolin */

#include <ffbase/map.h>

#define errlog0(...)  fmed_errlog(core, NULL, "core", __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, "core", __VA_ARGS__)

static fmed_core _core;

struct core_ctx {
	fmed_props props;
	ffmap ext_filter; // char[] -> struct filter_pair*
};
static struct core_ctx *cx;

#include "mods.h"

fmed_core* core_init()
{
	FF_ASSERT(cx == NULL);
	cx = ffmem_new(struct core_ctx);
	cx->props.codepage = FFUNICODE_WIN1252;
	_core.props = &cx->props;
	core = &_core;
	mods_init();
	return &_core;
}

void core_destroy()
{
	if (cx == NULL) return;

	ffmap_free(&cx->ext_filter);
	ffmem_free(cx);
	cx = NULL;
}

static ffssize core_cmd(uint cmd, ...)
{
	dbglog0("%s: %u", __func__, cmd);

	ffssize r = -1;
	va_list va;
	va_start(va, cmd);

	switch (cmd) {

	case FMED_IFILTER_BYEXT: {
		const char *sz = va_arg(va, char*);
		ffstr ext = FFSTR_INITZ(sz);
		r = (ffssize)mods_filter_byext(ext);
		break;
	}

	case FMED_FILTER_BYNAME:
		r = (ffssize)mods_filter_byname(va_arg(va, char*));
		break;

	default:
		errlog0("%s: bad command %u", __func__, cmd);
	}

	va_end(va);
	return r;
}

static fmed_core _core = {
	.getmod = mods_find,
	.cmd = core_cmd,
};
