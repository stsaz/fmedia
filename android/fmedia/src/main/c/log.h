/** fmedia/Android: logger
2022, Simon Zolin */

#include <android/log.h>

static void adrd_logv(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, va_list args)
{
	char buf[1024];
	ffsize cap = sizeof(buf) - 1;
	ffstr s = FFSTR_INITN(buf, 0);
	ffstr_addfmtv(&s, cap, fmt, args);

	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": (%u) %s", fferr_last(), fferr_strptr(fferr_last()));

	ffstr_addchar(&s, cap, '\0');

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

static void adrd_log(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	adrd_logv(flags, trk, module, fmt, args);
	va_end(args);
}
