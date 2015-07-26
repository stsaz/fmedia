/** fmedia interfaces.
Copyright (c) 2015 Simon Zolin */

/*
INPUT                 OUTPUT
file                  file
dsound  ->  core  ->  wasapi/dsound/alsa
mixer                 mixer
             ^
             v
           FILTERS
           raw/wav/ogg
           tui
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/data/parse.h>
#include <FFOS/file.h>
#include <FF/taskqueue.h>


#define FMED_VER  "0.2"

typedef struct fmed_core fmed_core;
typedef struct fmed_mod fmed_mod;
typedef struct fmed_filter fmed_filter;


typedef const fmed_mod* (*fmed_getmod_t)(const fmed_core *core);

enum FMED_SIG {
	FMED_OPEN
	, FMED_START
	, FMED_STOP
	, FMED_LISTDEV
};

enum FMED_TASK {
	FMED_TASK_DEL
	, FMED_TASK_POST
};

typedef struct fmed_modinfo {
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const fmed_filter *f;
} fmed_modinfo;

struct fmed_core {
	uint loglev;
	fffd kq;

	void (*log)(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...);

	/** Return NULL on error. */
	char* (*getpath)(const char *name, size_t len);

	/**
	@signo: enum FMED_SIG. */
	int (*sig)(uint signo);

	const fmed_modinfo* (*getmod)(const char *name);
	const fmed_modinfo* (*insmod)(const char *name, ffpars_ctx *ctx);

	/**
	@cmd: enum FMED_TASK. */
	void (*task)(fftask *task, uint cmd);
};


struct fmed_mod {
	const void* (*iface)(const char *name);

	/**
	@signo: enum FMED_SIG. */
	int (*sig)(uint signo);

	void (*destroy)(void);
};


enum {
	FMED_NULL = -1LL
};

#define FMED_PNULL ((void*)FMED_NULL)

typedef struct fmed_track {
	int64 (*popval)(void *trk, const char *name);

	/** Return FMED_NULL on error. */
	int64 (*getval)(void *trk, const char *name);

	/** Return FMED_PNULL on error. */
	const char* (*getvalstr)(void *trk, const char *name);

	int (*setval)(void *trk, const char *name, int64 val);
	int (*setvalstr)(void *trk, const char *name, const char *val);
} fmed_track;

#define fmed_getval(name)  (d)->track->getval((d)->trk, name)
#define fmed_popval(name)  (d)->track->popval((d)->trk, name)
#define fmed_setval(name, val)  (d)->track->setval((d)->trk, name, val)


typedef void (*fmed_handler)(void *udata);

enum FMED_F {
	FMED_FLAST = 1  //the last chunk of input data
};

typedef struct fmed_filt {
	const fmed_track *track;
	fmed_handler handler;
	void *trk;

	uint flags; //enum FMED_F

	size_t datalen;
	union {
	const char *data;
	void **datani; //non-iterleaved
	};

	size_t outlen;
	union {
	const char *out;
	void **outni;
	};
} fmed_filt;

enum FMED_R {
	FMED_ROK //output data is ready
	, FMED_RMORE //more input data is needed
	, FMED_RASYNC //an asynchronous operation is scheduled.  The module will call fmed_filt.handler.
	, FMED_RDONE //output data is completed, remove this module from the chain
	, FMED_RLASTOUT //output data is completed, remove this & all previous modules from the chain
	, FMED_RERR = -1 //fatal error, the track will be closed.
};

struct fmed_filter {
	/** Return NULL on error. */
	void* (*open)(fmed_filt *d);

	/** Return enum FMED_R. */
	int (*process)(void *ctx, fmed_filt *d);

	void (*close)(void *ctx);

	int (*conf)(ffpars_ctx *ctx);
};


static FFINL void fmed_setpcm(fmed_filt *d, const ffpcm *fmt)
{
	fmed_setval("pcm_format", fmt->format);
	fmed_setval("pcm_channels", fmt->channels);
	fmed_setval("pcm_sample_rate", fmt->sample_rate);
}

static FFINL void fmed_getpcm(const fmed_filt *d, ffpcm *fmt)
{
	fmt->format = fmed_getval("pcm_format");
	fmt->channels = fmed_getval("pcm_channels");
	fmt->sample_rate = fmed_getval("pcm_sample_rate");
}


enum FMED_LOG {
	FMED_LOG_DEBUG = 1
};

#define dbglog(core, trk, mod, ...) \
do { \
	if ((core)->loglev & FMED_LOG_DEBUG) \
		(core)->log(ffstdout, trk, mod, "debug", __VA_ARGS__); \
} while (0)

#define errlog(core, trk, mod, ...) \
	(core)->log(ffstderr, trk, mod, "error", __VA_ARGS__)

#define syserrlog(core, trk, mod, fmt, ...) \
	(core)->log(ffstderr, trk, mod, "error", fmt ": %E", __VA_ARGS__, fferr_last())
