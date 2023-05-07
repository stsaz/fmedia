/** fmedia: utility functions
2023, Simon Zolin */

#define FFSTR(s)  (char*)(s), FFS_LEN(s)

#include <FFOS/path.h>
static inline void ffpath_split3_str(ffstr fullname, ffstr *path, ffstr *name, ffstr *ext)
{
	ffstr nm;
	ffpath_splitpath_str(fullname, path, &nm);
	ffpath_splitname_str(nm, name, ext);
}
