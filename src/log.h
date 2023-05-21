/** fmedia: stdout/stderr log
2015, Simon Zolin */

#include <FFOS/std.h>

static const char* color_get(uint level)
{
	if (level & FMED_LOG_EXTRA)
		return FFSTD_CLR_I(FFSTD_BLUE);

	static const char colors[][8] = {
		/*FMED_LOG_ERR*/	FFSTD_CLR(FFSTD_RED),
		/*FMED_LOG_WARN*/	FFSTD_CLR(FFSTD_YELLOW),
		/*FMED_LOG_USER*/	"",
		/*FMED_LOG_INFO*/	FFSTD_CLR(FFSTD_GREEN),
		/*FMED_LOG_DEBUG*/	"",
	};
	FF_ASSERT(FF_COUNT(colors) == FMED_LOG_DEBUG);
	level &= _FMED_LOG_LEVMASK;
	return colors[level - FMED_LOG_ERR];
}

// TIME :TID [LEVEL] MOD: *ID: {"IN_FILENAME": } TEXT
static void std_log(uint flags, fmed_logdata *ld)
{
	uint level = flags & _FMED_LOG_LEVMASK;
	uint std_out = !!(level > FMED_LOG_USER && !core->props->stdout_busy);

	char buf[4096];
	ffuint cap = sizeof(buf) - 1;
	ffstr s = FFSTR_INITN(buf, 0);

	const char *color_end = "";
	if ((std_out && core->props->stdout_color)
		|| (!std_out && core->props->stderr_color)) {
		const char *color = color_get(flags);
		if (color[0] != '\0') {
			ffstr_addz(&s, cap, color);
			color_end = FFSTD_CLR_RESET;
		}
	}

	if (flags != FMED_LOG_USER) {
		if (ld->tid != 0) {
			ffstr_addfmt(&s, cap, "%s :%U [%s] %s: "
				, ld->stime, ld->tid, ld->level, ld->module);
		} else {
			ffstr_addfmt(&s, cap, "%s [%s] %s: "
				, ld->stime, ld->level, ld->module);
		}

		if (ld->ctx != NULL)
			ffstr_addfmt(&s, cap, "%S:\t", ld->ctx);
	}

	if (level <= FMED_LOG_USER && ld->trk != NULL) {
		const char *infn = g->track->getvalstr(ld->trk, "input");
		if (infn != FMED_PNULL)
			ffstr_addfmt(&s, cap, "\"%s\": ", infn);
	}

	ffstr_addfmtv(&s, cap, ld->fmt, ld->va);

	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": %E", fferr_last());

	ffstr_addz(&s, cap, color_end);

	s.ptr[s.len++] = '\n';

	fffd fd = (std_out) ? ffstdout : ffstderr;
	ffstd_write(fd, s.ptr, s.len);
}

static const fmed_log std_logger = {
	std_log
};
