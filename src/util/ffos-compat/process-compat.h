/** Create a copy of the current process in background
Return child process descriptor (parent)
  0 (child)
  -1 on error */
static ffps ffps_createself_bg(const char *arg);

#define FFPS_INV  FFPS_NULL

#ifdef FF_WIN

/* Create a copy of the current process with @arg appended to process' command line. */
static inline ffps ffps_createself_bg(const char *arg)
{
	ffps ps = FFPS_NULL;
	ffsize cap, arg_len = ffsz_len(arg), psargs_len;
	wchar_t *args, *p, *ps_args, fn[1024];

	if (0 == GetModuleFileNameW(NULL, fn, FF_COUNT(fn)))
		return FFPS_NULL;

	ps_args = GetCommandLineW();
	psargs_len = ffwsz_len(ps_args);
	cap = psargs_len + 1 + arg_len + 1;
	if (NULL == (args = (wchar_t*)ffmem_alloc(cap * sizeof(wchar_t))))
		return FFPS_NULL;
	p = args;
	ffmem_copy(p, ps_args, psargs_len * sizeof(wchar_t));
	p += psargs_len;
	*p++ = ' ';
	p += ff_utow(p, args + cap - p, arg, arg_len, 0);
	*p++ = '\0';

	ffps_execinfo info = {};
	info.in = info.out = info.err = INVALID_HANDLE_VALUE;
	ps = _ffps_exec_cmdln(fn, args, &info);

	ffmem_free(args);
	return ps;
}

/** Reset or disable a system timer.
flags:
 ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED: don't put the system to sleep
 ES_DISPLAY_REQUIRED: don't switch off display
 ES_CONTINUOUS:
  0: reset once
  1 + flags: disable
  1 + no flags: restore default behaviour
*/
#define ffps_systimer(flags)  SetThreadExecutionState(flags)

#else

#include <sys/stat.h>
#include <sys/fcntl.h>

static inline ffps ffps_createself_bg(const char *arg)
{
	(void)arg;
	ffps ps = fork();
	if (ps == 0) {
		setsid();
		umask(0);

		int f;
		if (-1 == (f = open("/dev/null", O_RDWR))) {
			return FFPS_NULL;
		}
		dup2(f, 0);
		dup2(f, 1);
		dup2(f, 2);
		if (f > 2)
			close(f);
	}
	return ps;
}

#endif

#include <FFOS/perf.h>
#include <FFOS/dylib.h>
#include <FFOS/environ.h>
#include <FFOS/sysconf.h>

#define ffenv  int
#define ffenv_destroy(a)
#define fflang_info  ffenv_locale
#define FFLANG_FLANG  FFENV_LANGUAGE
#define ffsc_init  ffsysconf_init
#define ffsc_get  ffsysconf_get
