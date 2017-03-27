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
#include <FF/sys/taskqueue.h>
#include <FFOS/file.h>
#include <FFOS/error.h>


#define FMED_VER_MINOR  24
#define FMED_VER  "0.24"
#define FMED_HOMEPAGE  "http://fmedia.firmdev.com"

// CORE
// TRACK
// FILTER
// LOG
// ADEV
// QUEUE
// GLOBCMD


// CORE

#define FMED_GLOBCONF  "fmedia.conf"

typedef struct fmed_core fmed_core;
typedef struct fmed_props fmed_props;
typedef struct fmed_mod fmed_mod;
typedef struct fmed_filter fmed_filter;
typedef const fmed_mod* (*fmed_getmod_t)(const fmed_core *core);

enum FMED_SIG {
	FMED_SIG_INIT, //initialize module data
	FMED_CONF, //args: "char *filename"
	FMED_OPEN
	, FMED_START
	, FMED_STOP
	,
	FMED_SIG_INSTALL,
	FMED_SIG_UNINSTALL,

	/**
	args: "char *filename"
	Return enum FMED_FT. */
	FMED_FILETYPE,
};

enum FMED_FT {
	FMED_FT_UKN,
	FMED_FT_PLIST,
	FMED_FT_DIR,
	FMED_FT_FILE,
};

enum FMED_TASK {
	FMED_TASK_DEL
	, FMED_TASK_POST
};

typedef struct fmed_modinfo {
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const void *iface;
} fmed_modinfo;

enum FMED_GETMOD {
	FMED_MOD_INFO = 1, /** Get fmed_modinfo*. */
	FMED_MOD_SOINFO,
	FMED_MOD_IFACE, /** Get module's interface (configured only). */
	FMED_MOD_IFACE_ANY, /** Get module's interface. */

	/** Get fmed_modinfo* by input/output file extension. */
	FMED_MOD_INEXT,
	FMED_MOD_OUTEXT,

	/** Get fmed_modinfo* of audio input/output module. */
	FMED_MOD_INFO_ADEV_OUT,
	FMED_MOD_INFO_ADEV_IN,

	FMED_MOD_NOLOG = 0x100,
};

struct fmed_core {
	uint loglev;
	fmed_props *props;
	fffd kq;

	int64 (*getval)(const char *name);

	/**
	@flags: enum FMED_LOG.
	*/
	void (*log)(uint flags, void *trk, const char *module, const char *fmt, ...);

	/** Return NULL on error. */
	char* (*getpath)(const char *name, size_t len);

	/**
	@signo: enum FMED_SIG. */
	int (*sig)(uint signo);

	/**
	@cmd: enum FMED_SIG. */
	ssize_t (*cmd)(uint cmd, ...);

	/** Get module interface. */
	const void* (*getmod)(const char *name);

	/**
	@flags: enum FMED_GETMOD.
	*/
	const void* (*getmod2)(uint flags, const char *name, ssize_t name_len);

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

struct fmed_props {
	uint stdout_busy :1;
	uint stdin_busy :1;
};

struct fmed_mod {
	const void* (*iface)(const char *name);

	/**
	@signo: enum FMED_SIG. */
	int (*sig)(uint signo);

	void (*destroy)(void);

	int (*conf)(const char *name, ffpars_ctx *ctx);
};


// TRACK

enum {
	FMED_NULL = -1LL
};

#define FMED_PNULL ((void*)FMED_NULL)

enum FMED_TRACK {
	FMED_TRACK_OPEN,
	FMED_TRACK_REC,
	FMED_TRACK_MIX,
	FMED_TRACK_NET,

	FMED_TRACK_START,
	FMED_TRACK_STOP,
	FMED_TRACK_PAUSE,
	FMED_TRACK_UNPAUSE,

	/** Stop active tracks.  Exit if recording.
	@trk:
	 . NULL: all playing
	 . (void*)-1: all (playing and recording). */
	FMED_TRACK_STOPALL,
	FMED_TRACK_STOPALL_EXIT,

	FMED_TRACK_LAST, //the last track in queue is finished

	/** Add new filter to the track chain.
	@param: char *filter_name */
	FMED_TRACK_ADDFILT,
	FMED_TRACK_ADDFILT_PREV,
	FMED_TRACK_ADDFILT_BEGIN,
};

enum FMED_TRK_TYPE {
	FMED_TRK_TYPE_REC = 1,
	FMED_TRK_TYPE_MIXIN,
	FMED_TRK_TYPE_MIXOUT,
	FMED_TRK_TYPE_NETIN,
};

enum FMED_TRK_FVAL {
	FMED_TRK_FACQUIRE = 2, //acquire pointer (value will be deleted with ffmem_free())
	FMED_TRK_FNO_OVWRITE = 4, //don't overwrite if already exists
	FMED_TRK_NAMESTR = 8,
	FMED_TRK_VALSTR = 0x10,
	FMED_TRK_META = 0x20,
};

#define FMED_TRK_ETMP  NULL // transient/system error
#define FMED_TRK_EFMT  ((void*)-1) // format is unsupported

typedef struct fmed_trk fmed_trk;
typedef fmed_trk fmed_filt;

typedef struct fmed_track {
	/**
	@cmd: enum FMED_TRACK.
	Return track ID;  FMED_TRK_E* on error. */
	void* (*create)(uint cmd, const char *url);

	fmed_trk* (*conf)(void *trk);

	void (*copy_info)(fmed_trk *dst, const fmed_trk *src);

	/**
	@cmd: enum FMED_TRACK. */
	int (*cmd)(void *trk, uint cmd);
	int (*cmd2)(void *trk, uint cmd, void *param);

	int64 (*popval)(void *trk, const char *name);

	/** Return FMED_NULL on error. */
	int64 (*getval)(void *trk, const char *name);

	/** Return FMED_PNULL on error. */
	const char* (*getvalstr)(void *trk, const char *name);

	int (*setval)(void *trk, const char *name, int64 val);

	int (*setvalstr)(void *trk, const char *name, const char *val);

	/**
	@flags: enum FMED_TRK_FVAL */
	int64 (*setval4)(void *trk, const char *name, int64 val, uint flags);
	char* (*setvalstr4)(void *trk, const char *name, const char *val, uint flags);

	char* (*getvalstr3)(void *trk, const void *name, uint flags);

	void (*loginfo)(void *trk, const ffstr **id, const char **module);
} fmed_track;

#define fmed_getval(name)  (d)->track->getval((d)->trk, name)
#define fmed_popval(name)  (d)->track->popval((d)->trk, name)
#define fmed_setval(name, val)  (d)->track->setval((d)->trk, name, val)


// FILTER

typedef void (*fmed_handler)(void *udata);

enum FMED_F {
	FMED_FLAST = 1, // the last chunk of input data
	FMED_FSTOP = 2, // track is being stopped
	FMED_FFWD = 4,
};

/** >0: msec;  <0: CD frames (1/75 sec) */
typedef int64 fmed_apos;

static FFINL uint64 fmed_apos_samples(fmed_apos val, uint rate)
{
	if (val > 0)
		return ffpcm_samples(val, rate);
	else
		return -val * rate / 75;
}

struct fmed_trk {
	const fmed_track *track;
	fmed_handler handler;
	void *trk;

	uint flags; //enum FMED_F
	uint type; //enum FMED_TRK_TYPE
	struct {
		ffpcmex fmt;
		ffpcmex convfmt_in; //the format used as input to the next converter (e.g. for conv -> soxr)
		ffpcmex convfmt; //format of audio data produced by converter filter
		uint64 pos; //samples
		uint64 total; //samples
		uint64 seek; //msec
		fmed_apos until;
		fmed_apos abs_seek; //seek position from the beginning of file
		uint gain; //dB * 100
		float maxpeak; //dB
		uint bitrate; //bit/s
	} audio;

	struct {
		int quality;
		short bandwidth;
	} aac;
	struct {
		signed char quality; // (q+1.0)*10
	} vorbis;
	struct {
		short bitrate;
		short frame_size;
		signed char bandwidth;
	} opus;
	struct {
		short quality;
	} mpeg;
	struct {
		signed char compression;
		signed char md5;
	} flac;

	struct {
		uint64 size;
		uint64 seek;
	} input, output;
	union {
	uint bits;
	struct {
		uint input_info :1;
		uint out_preserve_date :1;
		uint out_overwrite :1;
		uint snd_output_clear :1;
		uint snd_output_pause :1;
		uint meta_changed :1;
		uint pcm_peaks_crc :1;
		uint out_seekable :1;
		uint meta_block :1; //data block isn't audio
	};
	};

//fmed_filt only:
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
};

enum FMED_R {
	FMED_ROK //output data is ready.  The module will be called again if there's unprocessed input data.
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
};

static FFINL int64 fmed_popval_def(fmed_filt *d, const char *name, int64 def)
{
	int64 n;
	if (FMED_NULL != (n = d->track->popval(d->trk, name)))
		return n;
	return def;
}


// LOG

enum FMED_LOG {
	FMED_LOG_ERR = 1,
	FMED_LOG_WARN,
	FMED_LOG_USER,
	FMED_LOG_INFO,
	FMED_LOG_DEBUG,
	_FMED_LOG_LEVMASK = 0x0f,

	FMED_LOG_SYS = 0x10,
};

#define dbglog(core, trk, mod, ...) \
do { \
	if ((core)->loglev == FMED_LOG_DEBUG) \
		(core)->log(FMED_LOG_DEBUG, trk, mod, __VA_ARGS__); \
} while (0)

#define warnlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_WARN, trk, mod, __VA_ARGS__)

#define errlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_ERR, trk, mod, __VA_ARGS__)

#define syserrlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_ERR | FMED_LOG_SYS, trk, mod, __VA_ARGS__)

typedef struct fmed_logdata {
	const char *level;
	const char *stime;
	const char *module;
	const ffstr *ctx;
	const char *fmt;
	va_list va;
} fmed_logdata;

typedef struct fmed_log {
	void (*log)(uint flags, fmed_logdata *ld);
} fmed_log;


// ADEV

enum FMED_ADEV_F {
	FMED_ADEV_PLAYBACK,
	FMED_ADEV_CAPTURE,
};

typedef struct fmed_adev_ent {
	char *name;
} fmed_adev_ent;

typedef struct fmed_adev {
	/**
	@flags: enum FMED_ADEV_F
	*/
	int (*list)(fmed_adev_ent **ents, uint flags);
	void (*listfree)(fmed_adev_ent *ents);
} fmed_adev;


// QUEUE

typedef struct fmed_que_entry {
	ffstr url;
	int from // >0: msec;  <0: CD frames (1/75 sec)
		, to;
	int dur; //msec
	void *prev;
	fmed_trk *trk;
} fmed_que_entry;

enum FMED_QUE_EVT {
	FMED_QUE_ONADD,
	FMED_QUE_ONRM,
	FMED_QUE_ONCLEAR,
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
	FMED_QUE_ADD,
	FMED_QUE_RM,
	FMED_QUE_RMDEAD,
	FMED_QUE_METASET, // @param2: ffstr name_val_pair[2]
	FMED_QUE_SETONCHANGE, // @param: fmed_que_onchange_t
	FMED_QUE_EXPAND, // @param: fmed_que_entry*

	FMED_QUE_NEW,
	FMED_QUE_DEL, // @param: uint
	FMED_QUE_SEL, // @param: uint
	FMED_QUE_LIST, // @param: fmed_que_entry*
};

enum FMED_QUE_CMDF {
	_FMED_QUE_FMASK = 0xffff0000,
	FMED_QUE_NO_ONCHANGE = 0x10000,
	FMED_QUE_ADD_DONE = 0x20000,
	FMED_QUE_COPY_PROPS = 0x40000, //copy track properties from fmed_que_entry.prev
};

enum FMED_QUE_META_F {
	FMED_QUE_TMETA = 1,
	FMED_QUE_OVWRITE = 2,
	FMED_QUE_UNIQ = 4,
	FMED_QUE_TRKDICT = 8,
	FMED_QUE_NUM = 0x10,
	FMED_QUE_METADEL = 0x20,
	FMED_QUE_NO_TMETA = 0x40, //don't include transient meta
};

#define FMED_QUE_SKIP  ((void*)-1)

typedef struct fmed_queue {
	/**
	@cmd: enum FMED_QUE + enum FMED_QUE_CMDF
	*/
	ssize_t (*cmd2)(uint cmd, void *param, size_t param2);

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

enum FMED_GUI_SIG {
	FMED_GUI_SHOW = 1 << 31,
};


// GLOBCMD

enum FMED_GLOBCMD {
	FMED_GLOBCMD_OPEN,
	FMED_GLOBCMD_START,
};

typedef struct fmed_globcmd_iface {
	/**
	@cmd: enum FMED_GLOBCMD. */
	int (*ctl)(uint cmd);
	int (*write)(const void *data, size_t len);
} fmed_globcmd_iface;
