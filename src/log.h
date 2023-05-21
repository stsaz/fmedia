/** fmedia: stdout/stderr log
2015, Simon Zolin */

// TIME :TID [LEVEL] MOD: *ID: {"IN_FILENAME": } TEXT
static void std_log(uint flags, fmed_logdata *ld)
{
	char buf[4096];
	ffuint cap = sizeof(buf) - 1;
	ffstr s = FFSTR_INITN(buf, 0);

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

	if ((flags & _FMED_LOG_LEVMASK) <= FMED_LOG_USER && ld->trk != NULL) {
		const char *infn = g->track->getvalstr(ld->trk, "input");
		if (infn != FMED_PNULL)
			ffstr_addfmt(&s, cap, "\"%s\": ", infn);
	}

	ffstr_addfmtv(&s, cap, ld->fmt, ld->va);

	if (flags & FMED_LOG_SYS)
		ffstr_addfmt(&s, cap, ": %E", fferr_last());

	s.ptr[s.len++] = '\n';

	uint lev = flags & _FMED_LOG_LEVMASK;
	fffd fd = (lev > FMED_LOG_USER && !core->props->stdout_busy) ? ffstdout : ffstderr;
	ffstd_write(fd, s.ptr, s.len);
}

static const fmed_log std_logger = {
	std_log
};
