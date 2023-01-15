/** fmedia/Android: core
2022, Simon Zolin */

#include <ffbase/map.h>

#define errlog0(...)  fmed_errlog(core, NULL, "core", __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, "core", __VA_ARGS__)

static fmed_core _core;

struct wrk_ctx;
struct core_ctx {
	fmed_props props;
	ffmap ext_filter; // char[] -> struct filter_pair*
	struct wrk_ctx *wx;
};
static struct core_ctx *cx;

#include "mods.h"
#include "worker.h"

fmed_core* core_init()
{
	FF_ASSERT(cx == NULL);
	cx = ffmem_new(struct core_ctx);
	cx->props.codepage = FFUNICODE_WIN1252;
	_core.props = &cx->props;
	core = &_core;
	cx->wx = wrkx_init();
	mods_init();
	return &_core;
}

void core_destroy()
{
	if (cx == NULL) return;

	wrkx_destroy(cx->wx);
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
		r = (ffssize)mods_ifilter_byext(ext);
		break;
	}

	case FMED_OFILTER_BYEXT: {
		const char *sz = va_arg(va, char*);
		ffstr ext = FFSTR_INITZ(sz);
		r = (ffssize)mods_ofilter_byext(ext);
		break;
	}

	case FMED_FILTER_BYNAME:
		r = (ffssize)mods_filter_byname(va_arg(va, char*));
		break;

	case FMED_XASSIGN:
		wrkx_assign(cx->wx, va_arg(va, void*));
		r = 0;
		break;

	case FMED_XADD: {
		void *wt = va_arg(va, void*);
		void *func = va_arg(va, void*);
		void *udata = va_arg(va, void*);
		wrkx_add(cx->wx, wt, func, udata);
		r = 0;
		break;
	}
	case FMED_XDEL:
		wrkx_del(cx->wx, va_arg(va, void*));
		r = 0;
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
