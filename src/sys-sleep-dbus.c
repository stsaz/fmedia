/** Disable sleep timer on Linux via D-BUS.
Copyright (c) 2020 Simon Zolin */


#include <fmedia.h>
#include <FFOS/process.h>


#undef dbglog
#undef errlog
#define dbglog(...)  fmed_dbglog(core, NULL, "dbussleep", __VA_ARGS__)
#define errlog(...)  fmed_errlog(core, NULL, "dbussleep", __VA_ARGS__)

static const fmed_core *core;

// FMEDIA MODULE
static const void* dbussleep_iface(const char *name);
static int dbussleep_sig(uint signo);
static void dbussleep_destroy(void);
static const fmed_mod fmed_dbussleep_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&dbussleep_iface, &dbussleep_sig, &dbussleep_destroy, NULL
};

// TRACK FILTER
static void* dbussleep_open(fmed_filt *d);
static void dbussleep_close(void *ctx);
static int dbussleep_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_dbussleep = {
	&dbussleep_open, &dbussleep_process, &dbussleep_close
};

static void allowsleep(void *param);


#define ALLOWSLEEP_TIMEOUT  5000


struct dbussleep {
	fftmrq_entry tmr;
	uint ntracks;
	struct ffps_systimer st;
};

static struct dbussleep *g;

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_dbussleep_mod;
}

static int dbussleep_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (!core->props->prevent_sleep)
			break;
		g = ffmem_new(struct dbussleep);
		g->st.appname = "fmedia";
		g->st.reason = "audio playback";
		break;
	}
	return 0;
}

static void dbussleep_destroy(void)
{
	if (g == NULL)
		return;
	allowsleep(NULL);
	ffps_systimer_close(&g->st);
	ffmem_free0(g);
}

static const void* dbussleep_iface(const char *name)
{
	if (!ffsz_cmp(name, "sleep"))
		return &fmed_dbussleep;
	return NULL;
}

static void allowsleep(void *param)
{
	if (g->tmr.handler == NULL)
		return;
	g->tmr.handler = NULL;
	dbglog("resume system sleep timer");
	ffps_systimer(&g->st, 0);
}


void* dbussleep_open(fmed_filt *d)
{
	if (g == NULL)
		return FMED_FILT_SKIP;

	if (g->ntracks == 0) {
		core->timer(&g->tmr, 0, 0);
		if (g->tmr.handler == NULL) {
			if (0 != ffps_systimer(&g->st, 1)) {
				errlog("ffps_systimer()");
				return FMED_FILT_SKIP;
			}
			dbglog("pause system sleep timer");
		}
	}

	g->ntracks++;
	return FMED_FILT_DUMMY;
}

void dbussleep_close(void *ctx)
{
	if (--g->ntracks == 0) {
		g->tmr.handler = &allowsleep;
		core->timer(&g->tmr, -ALLOWSLEEP_TIMEOUT, 0);
	}
}

int dbussleep_process(void *ctx, fmed_filt *d)
{
	return FMED_RDONE;
}
