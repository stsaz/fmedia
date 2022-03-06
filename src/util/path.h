#pragma once
#include <FFOS/path.h>

/** Get filename and directory (without the last slash). */
static inline const char* ffpath_split2(const char *fn, size_t len, ffstr *dir, ffstr *name)
{
	ffssize r = ffpath_splitpath(fn, len, dir, name);
	if (r < 0)
		return NULL;
	return &fn[r];
}

static inline const char* ffpath_split3(const char *fullname, size_t len, ffstr *path, ffstr *name, ffstr *ext)
{
	ffstr nm;
	const char *slash = ffpath_split2(fullname, len, path, &nm);
	ffpath_splitname(nm.ptr, nm.len, name, ext);
	return slash;
}
