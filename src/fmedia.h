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
#include <FFOS/error.h>
#include <FF/taskqueue.h>


#define FMED_VER  "0.8"

typedef struct fmed_core fmed_core;
typedef struct fmed_mod fmed_mod;
typedef struct fmed_filter fmed_filter;


typedef const fmed_mod* (*fmed_getmod_t)(const fmed_core *core);

enum FMED_SIG {
	FMED_OPEN
	, FMED_CONF
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

	int64 (*getval)(const char *name);

	void (*log)(fffd fd, void *trk, const char *module, const char *level, const char *fmt, ...);

	/** Return NULL on error. */
	char* (*getpath)(const char *name, size_t len);

	/**
	@signo: enum FMED_SIG. */
	int (*sig)(uint signo);

	/** Get module (fmed_modinfo*) or an interface. */
	const void* (*getmod)(const char *name);
	const fmed_modinfo* (*insmod)(const char *name, ffpars_ctx *ctx);

	/**
	@cmd: enum FMED_TASK. */
	void (*task)(fftask *task, uint cmd);
};

enum FMED_INSTANCE_MODE {
	FMED_IM_OFF,
	FMED_IM_ADD,
	FMED_IM_PLAY,
	FMED_IM_CLEARPLAY,
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

enum FMED_TRACK {
	FMED_TRACK_OPEN,
	FMED_TRACK_REC,
	FMED_TRACK_MIX,

	FMED_TRACK_START,
	FMED_TRACK_STOP,

	/** @trk:
	 . NULL: all playing
	 . (void*)-1: all (playing and recording). */
	FMED_TRACK_STOPALL,
	FMED_TRACK_STOPALL_EXIT,
};

enum FMED_TRK_TYPE {
	FMED_TRK_TYPE_REC = 1,
	FMED_TRK_TYPE_MIXIN,
};

enum FMED_TRK_FVAL {
	FMED_TRK_FACQUIRE = 2, //acquire pointer (value will be deleted with ffmem_free())
	FMED_TRK_FNO_OVWRITE = 4, //don't overwrite if already exists
};

#define FMED_TRK_ETMP  NULL // transient/system error
#define FMED_TRK_EFMT  ((void*)-1) // format is unsupported

typedef struct fmed_track {
	/**
	@cmd: enum FMED_TRACK.
	Return track ID;  FMED_TRK_E* on error. */
	void* (*create)(uint cmd, const char *url);

	/**
	@cmd: enum FMED_TRACK. */
	int (*cmd)(void *trk, uint cmd);

	int64 (*popval)(void *trk, const char *name);

	/** Return FMED_NULL on error. */
	int64 (*getval)(void *trk, const char *name);

	/** Return FMED_PNULL on error. */
	const char* (*getvalstr)(void *trk, const char *name);

	int (*setval)(void *trk, const char *name, int64 val);

	int (*setvalstr)(void *trk, const char *name, const char *val);

	/**
	@flags: enum FMED_TRK_FVAL */
	char* (*setvalstr4)(void *trk, const char *name, const char *val, uint flags);
} fmed_track;

#define fmed_getval(name)  (d)->track->getval((d)->trk, name)
#define fmed_popval(name)  (d)->track->popval((d)->trk, name)
#define fmed_setval(name, val)  (d)->track->setval((d)->trk, name, val)


typedef void (*fmed_handler)(void *udata);

enum FMED_F {
	FMED_FLAST = 1, // the last chunk of input data
	FMED_FSTOP = 2, // track is being stopped
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
	, FMED_RDATA //output data is ready, the module will be called again
	, FMED_RMORE //more input data is needed
	, FMED_RASYNC //an asynchronous operation is scheduled.  The module will call fmed_filt.handler.
	, FMED_RDONE //output data is completed, remove this module from the chain
	, FMED_RDONE_PREV //the same as FMED_RDONE, but move backward through the chain
	, FMED_RLASTOUT //output data is completed, remove this & all previous modules from the chain
	, FMED_RFIN //close the track
	, FMED_RSYSERR //system error.  Print error message and close the track.
	, FMED_RERR = -1 //fatal error, the track will be closed.
};

#define FMED_FILT_SKIP  ((void*)-1)
#define FMED_FILT_DUMMY  ((void*)-2)

struct fmed_filter {
	/** Return NULL on error.
	 Return FMED_FILT_SKIP to skip this filter. */
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

#define warnlog(core, trk, mod, ...) \
	(core)->log(ffstderr, trk, mod, "warning", __VA_ARGS__)

#define errlog(core, trk, mod, ...) \
	(core)->log(ffstderr, trk, mod, "error", __VA_ARGS__)

#define syserrlog(core, trk, mod, fmt, ...) \
	(core)->log(ffstderr, trk, mod, "error", fmt ": %E", __VA_ARGS__, fferr_last())

typedef struct fmed_log {
	void (*log)(const char *stime, const char *module, const char *level, const ffstr *id,
		const char *fmt, va_list va);
} fmed_log;


typedef struct fmed_que_entry {
	ffstr url;
	ffstr *meta; //name,value,...
	uint nmeta;
	int from // >0: msec;  <0: CD frames (1/75 sec)
		, to;
	int dur; //msec
} fmed_que_entry;

enum FMED_QUE_EVT {
	FMED_QUE_ONADD,
	FMED_QUE_ONRM,
};

/** @flags: enum FMED_QUE_EVT. */
typedef void (*fmed_que_onchange_t)(fmed_que_entry *e, uint flags);

enum FMED_QUE {
	FMED_QUE_PLAY,
	FMED_QUE_PLAY_EXCL,
	FMED_QUE_MIX,
	FMED_QUE_STOP_AFTER,
	FMED_QUE_NEXT,
	FMED_QUE_PREV,
	FMED_QUE_SAVE, //save playlist to file, @param: "const char *filename"
	FMED_QUE_CLEAR,
	FMED_QUE_RM,
	FMED_QUE_SETONCHANGE, // @param: fmed_que_onchange_t
};

enum FMED_QUE_META_F {
	FMED_QUE_TMETA = 1,
	FMED_QUE_OVWRITE = 2,
	FMED_QUE_UNIQ = 4,
	FMED_QUE_TRKDICT = 8,
	FMED_QUE_NUM = 0x10,
};

#define FMED_QUE_SKIP  ((void*)-1)

typedef struct fmed_queue {
	fmed_que_entry* (*add)(fmed_que_entry *ent);

	/** @cmd: enum FMED_QUE */
	void (*cmd)(uint cmd, void *param);

	/** flags: enum FMED_QUE_META_F */
	void (*meta_set)(fmed_que_entry *ent, const char *name, size_t name_len, const char *val, size_t val_len, uint flags);
	ffstr* (*meta_find)(fmed_que_entry *ent, const char *name, size_t name_len);

	/** Get meta.
	@flags: enum FMED_QUE_META_F.
	Return meta value;  NULL: no more entries;  FMED_QUE_SKIP: skip this entry. */
	ffstr* (*meta)(fmed_que_entry *ent, size_t n, ffstr *name, uint flags);
} fmed_queue;
