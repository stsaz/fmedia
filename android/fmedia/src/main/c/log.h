/** fmedia/Android: logger
2022, Simon Zolin */

#include <android/log.h>

static void adrd_logv(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, va_list args)
{
	ffstr s = {};
	ffsize cap = 0;
	ffsize r = ffstr_growfmtv(&s, &cap, fmt, args);
	if (r != 0) {
		ffstr_growaddchar(&s, &cap, '\0');
		uint level = ANDROID_LOG_INFO;
		switch (flags & _FMED_LOG_LEVMASK) {
		case FMED_LOG_ERR:
			level = ANDROID_LOG_ERROR; break;
		case FMED_LOG_WARN:
			level = ANDROID_LOG_WARN; break;
		case FMED_LOG_DEBUG:
			level = ANDROID_LOG_DEBUG; break;
		}
		__android_log_print(level, "fmedia", "%s", s.ptr);
	}
	ffstr_free(&s);
}

static void adrd_log(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	adrd_logv(flags, trk, module, fmt, args);
	va_end(args);
}

#define syserrlog1(trk, ...)  fmed_syserrlog(core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define warnlog1(trk, ...)  fmed_warnlog(core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
