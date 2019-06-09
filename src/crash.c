/** Crash handler.
Copyright (c) 2018 Simon Zolin */

#include <FF/string.h>
#include <FFOS/sig.h>
#include <FFOS/time.h>
#include <FFOS/file.h>
#include <FFOS/process.h>


void _crash_handler(const char *fullname, struct ffsig_info *inf)
{
	size_t n;
	char buf[512], fn[512], *p, *end;
	fffd f;

	fftime t;
	fftime_now(&t);

	end = buf + sizeof(buf);

#ifdef FF_WIN
	p = ffenv_expand(NULL, fn, sizeof(fn), "%TMP%\\");
	if (p == NULL)
		p = ffmem_copy(fn, "c:\\", 3);
	else
		p += ffsz_len(p);
#else
	p = ffmem_copy(fn, "/tmp/", 5);
#endif

	ffs_fmt(p, fn + sizeof(fn), "fmedia-crashdump-%xU.txt%Z"
		, (int64)t.sec);
	f = fffile_open(fn, FFO_CREATE | FFO_TRUNC | FFO_WRONLY);
	if (f == FF_BADFD)
		f = ffstderr;
	else {
		n = ffs_fmt(buf, end, "fmedia has crashed: %s\n"
			, fn);
		fffile_write(ffstderr, buf, n);
	}

	n = ffs_fmt(buf, end, "%s\n", fullname);
	fffile_write(f, buf, n);

	n = ffs_fmt(buf, end, "Signal:%xu  Address:0x%p  Flags:%xu\n"
		, inf->sig, inf->addr, inf->flags);
	fffile_write(f, buf, n);

	if (f != ffstderr)
		fffile_close(f);
}
