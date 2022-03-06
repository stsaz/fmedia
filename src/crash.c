/** Crash handler.
Copyright (c) 2018 Simon Zolin */

#include <FFOS/sig.h>
#include <FFOS/time.h>
#include <FFOS/file.h>
#include <FFOS/process.h>
#include <FFOS/backtrace.h>
#include <FFOS/thread.h>
// #include <FFOS/cpuid.h>


static ffthread_bt bt;

void _crash_handler(const char *fullname, const char *version, struct ffsig_info *inf)
{
	fffd f = FF_BADFD;
	char buf[512], fn[512], *p;
	ffstr s;
	ffstr_set(&s, buf, 0);

	fftime t;
	fftime_now(&t);

#ifdef FF_WIN
	p = ffenv_expand(NULL, fn, sizeof(fn), "%TMP%\\");
	if (p == NULL)
		p = ffmem_copy(fn, "c:\\", 3);
	else
		p += ffsz_len(p);
#else
	p = ffmem_copy(fn, "/tmp/", 5);
#endif

	// open file
	ffs_format(p, fn + sizeof(fn) - p, "fmedia-crashdump-%xU.txt%Z"
		, (int64)t.sec);
	f = fffile_open(fn, FFO_CREATE | FFO_TRUNC | FFO_WRONLY);
	if (f == FF_BADFD)
		f = ffstderr;
	else
		ffstr_addfmt(&s, sizeof(buf), "fmedia has crashed: %s\n", fn);

	// general info
	ffstr_addfmt(&s, sizeof(buf),
		"%s v%s\n"
		"Signal:%u  Address:0x%p  Flags:%xu  Thread:%xU\n"
		, fullname, version
		, inf->sig, inf->addr, inf->flags, ffthread_curid());
	fffile_write(f, s.ptr, s.len);
	if (f != ffstderr)
		fffile_write(ffstderr, s.ptr, s.len);
	s.len = 0;

	// backtrace
	int n = ffthread_backtrace(&bt);
	for (int i = 0;  i < n;  i++) {
		size_t offset = (char*)ffthread_backtrace_frame(&bt, i) - (char*)ffthread_backtrace_modbase(&bt, i);
		const ffsyschar *modname = ffthread_backtrace_modname(&bt, i);
		ffstr_addfmt(&s, sizeof(buf), "#%u: 0x%p +%xL %q [0x%p]\n"
			, i, ffthread_backtrace_frame(&bt, i), offset
			, modname, ffthread_backtrace_modbase(&bt, i));
		fffile_write(f, s.ptr, s.len);
		if (f != ffstderr)
			fffile_write(ffstderr, s.ptr, s.len);
		s.len = 0;
	}

#if 0
	// cpuid
	ffcpuid cpuid = {};
	if (0 == ff_cpuid(&cpuid, FFCPUID_VENDOR | FFCPUID_FEATURES | FFCPUID_BRAND)){
		s.len = 0;
		ffstr_addfmt(&s, sizeof(buf), "v:%s b:%s f:%*xb\n"
			, cpuid.vendor, cpuid.brand, sizeof(cpuid.features), cpuid.features);
		fffile_write(f, s.ptr, s.len);
		if (f != ffstderr)
			fffile_write(ffstderr, s.ptr, s.len);
	}
#endif

	if (f != ffstderr)
		fffile_close(f);
}
