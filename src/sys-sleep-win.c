/** Disable sleep timer on Windows.
Copyright (c) 2020 Simon Zolin */

/** Pause/resume system sleep timer.
Pause system sleep timer when the first track is created.
Arm the timer upon completion of the last track.
If a new track is created before the timer expires - deactivate the timer.
Otherwise, resume system sleep timer. */


#include <fmedia.h>
#include <FFOS/process.h>


#undef dbglog
#define dbglog(...)  fmed_dbglog(core, NULL, "winsleep", __VA_ARGS__)

static const fmed_core *core;

// FMEDIA MODULE
static const void* winsleep_iface(const char *name);
static int winsleep_sig(uint signo);
static void winsleep_destroy(void);
static const fmed_mod fmed_winsleep_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&winsleep_iface, &winsleep_sig, &winsleep_destroy, NULL
};

// TRACK FILTER
static void* winsleep_open(fmed_filt *d);
static void winsleep_close(void *ctx);
static int winsleep_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_winsleep = {
	&winsleep_open, &winsleep_process, &winsleep_close
};

static void allowsleep(void *param);

#define ALLOWSLEEP_TIMEOUT  5000

struct winsleep {
	fftimerqueue_node tmr;
	uint ntracks;
};

static struct winsleep *g;

const fmed_mod* fmed_getmod_winsleep(const fmed_core *_core)
{
	core = _core;
	return &fmed_winsleep_mod;
}

static int winsleep_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (!core->props->prevent_sleep)
			break;
		g = ffmem_new(struct winsleep);
		break;
	}
	return 0;
}

static void winsleep_destroy(void)
{
	if (g == NULL)
		return;
	allowsleep(NULL);
	ffmem_free0(g);
}

static const void* winsleep_iface(const char *name)
{
	if (!ffsz_cmp(name, "sleep"))
		return &fmed_winsleep;
	return NULL;
}

static void allowsleep(void *param)
{
	if (g->tmr.func == NULL)
		return;
	g->tmr.func = NULL;
	dbglog("resume system sleep timer");
	ffps_systimer(ES_CONTINUOUS);
}


void* winsleep_open(fmed_filt *d)
{
	if (g == NULL)
		return FMED_FILT_SKIP;

	if (g->ntracks++ == 0) {
		core->timer(&g->tmr, 0, 0);
		if (g->tmr.func == NULL) {
			ffps_systimer(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
			dbglog("pause system sleep timer");
		}
	}
	return FMED_FILT_DUMMY;
}

void winsleep_close(void *ctx)
{
	if (--g->ntracks == 0) {
		fmed_timer_set(&g->tmr, allowsleep, NULL);
		core->timer(&g->tmr, -ALLOWSLEEP_TIMEOUT, 0);
	}
}

int winsleep_process(void *ctx, fmed_filt *d)
{
	return FMED_RDONE;
}
