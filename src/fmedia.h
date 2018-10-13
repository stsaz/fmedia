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
#include <FF/sys/timer-queue.h>
#include <FFOS/file.h>
#include <FFOS/error.h>


#define FMED_VER_MAJOR  1
#define FMED_VER_MINOR  0
#define FMED_VER_FULL  ((FMED_VER_MAJOR << 8) | FMED_VER_MINOR)
#define FMED_VER  "1.0"

#define FMED_VER_GETMAJ(fullver)  ((fullver) >> 8)
#define FMED_VER_GETMIN(fullver)  ((fullver) & 0xff)

/** Inter-module compatibility version. */
#define FMED_VER_CORE  ((FMED_VER_MAJOR << 8) | 0)

#define FMED_HOMEPAGE  "http://fmedia.firmdev.com"

// CORE
// TRACK
// FILTER
// LOG
// ADEV
// QUEUE
// GLOBCMD
// NET


// CORE

#define FMED_GLOBCONF  "fmedia.conf"

typedef struct fmed_core fmed_core;
typedef struct fmed_props fmed_props;
typedef struct fmed_mod fmed_mod;
typedef struct fmed_filter fmed_filter;

#define FMED_MODFUNCNAME  "fmed_getmod" //name of the function which is exported by a module
typedef const fmed_mod* (*fmed_getmod_t)(const fmed_core *core);

enum FMED_SIG {
	FMED_SIG_INIT, //initialize module data
	FMED_CONF, // Read config.  args: "char *filename" (optional)
	FMED_OPEN,
	FMED_START,
	FMED_STOP,

	FMED_SIG_INSTALL,
	FMED_SIG_UNINSTALL,

	/**
	args: "char *filename"
	Return enum FMED_FT. */
	FMED_FILETYPE,

	FMED_WOH_INIT,
	/** Windows: add handle to WOH.
	args: "HANDLE h, fftask *task" */
	FMED_WOH_ADD,

	/** Windows: remove handle from WOH.
	args: "HANDLE h" */
	FMED_WOH_DEL,

	/** Add a cross-worker task.
	args: "fftask *task, uint wid" */
	FMED_TASK_XPOST,
};

enum FMED_FT {
	FMED_FT_UKN,
	FMED_FT_PLIST,
	FMED_FT_DIR,
	FMED_FT_FILE,
};

enum FMED_TASK {
	FMED_TASK_DEL,
	FMED_TASK_POST,
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

	char* (*env_expand)(char *dst, size_t cap, const char *src);

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

	/** Add task to the main worker.
	@cmd: enum FMED_TASK. */
	void (*task)(fftask *task, uint cmd);

	/** Set timer on the main worker.
	@interval:  >0: periodic;  <0: one-shot;  0: disable.
	Return 0 on success. */
	int (*timer)(fftmrq_entry *tmr, int64 interval, uint flags);
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
	char *version_str; // "X.XX[.XX]"
};

struct fmed_mod {
	uint ver;
	uint ver_core;

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

enum FMED_TRACK_CMD {
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
	Obsolete.
	@param: char *filter_name */
	FMED_TRACK_ADDFILT,
	FMED_TRACK_ADDFILT_PREV,
	FMED_TRACK_ADDFILT_BEGIN,

	/** Add new filter to the track chain.
	@param: char *filter_name
	Return filter ID;  NULL on error. */
	FMED_TRACK_FILT_ADD,
	FMED_TRACK_FILT_ADDPREV,

	FMED_TRACK_META_HAVEUSER,

	/**
	@param: fmed_trk_meta* */
	FMED_TRACK_META_ENUM,

	/**
	@param: void *src_track */
	FMED_TRACK_META_COPYFROM,

	FMED_TRACK_FILT_GETPREV, // get context pointer of the previous filter
	FMED_TRACK_FILT_INSTANCE, // get (create) filter instance.  @param: void *filter_id

	/** Continue track processing after suspend and an asynchronous event. */
	FMED_TRACK_WAKE,

	FMED_TRACK_MONITOR,

	FMED_TRACK_FILT_ADDFIRST,
	FMED_TRACK_FILT_ADDLAST,

	/** Get kernel queue associated with this track.
	Return fffd. */
	FMED_TRACK_KQ,
};

enum FMED_TRK_TYPE {
	FMED_TRK_TYPE_NONE,
	FMED_TRK_TYPE_PLAYBACK,
	FMED_TRK_TYPE_REC,
	FMED_TRK_TYPE_MIXIN,
	FMED_TRK_TYPE_MIXOUT,
	FMED_TRK_TYPE_NETIN,
	_FMED_TRK_TYPE_END,

	//obsolete:
	FMED_TRACK_OPEN = FMED_TRK_TYPE_PLAYBACK,
	FMED_TRACK_REC = FMED_TRK_TYPE_REC,
	FMED_TRACK_MIX = FMED_TRK_TYPE_MIXOUT,
	FMED_TRACK_NET = FMED_TRK_TYPE_NETIN,
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
	@cmd: enum FMED_TRK_TYPE.
	Return track ID;  FMED_TRK_E* on error. */
	void* (*create)(uint cmd, const char *url);

	fmed_trk* (*conf)(void *trk);

	void (*copy_info)(fmed_trk *dst, const fmed_trk *src);

	/**
	@cmd: enum FMED_TRACK_CMD. */
	ssize_t (*cmd)(void *trk, uint cmd, ...);
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

	/**
	@flags: enum FMED_QUE_META_F */
	void (*meta_set)(void *trk, const ffstr *name, const ffstr *val, uint flags);
} fmed_track;

#define fmed_getval(name)  (d)->track->getval((d)->trk, name)
#define fmed_popval(name)  (d)->track->popval((d)->trk, name)
#define fmed_setval(name, val)  (d)->track->setval((d)->trk, name, val)
#define fmed_trk_filt_prev(d, ptr)  (d)->track->cmd2((d)->trk, FMED_TRACK_FILT_GETPREV, ptr)

typedef struct fmed_trk_meta {
	ffstr name, val;
	void *trnod;
	void *qent;
	uint idx;
	uint flags;
} fmed_trk_meta;

enum FMED_TRK_MON {
	FMED_TRK_ONCLOSE,
	FMED_TRK_ONLAST,
};
struct fmed_trk_mon {
	void (*onsig)(fmed_trk *trk, uint sig);
};
/** Associate monitor interface with tracks. */
#define fmed_trk_monitor(trk, mon)  cmd(NULL, FMED_TRACK_MONITOR, mon)


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
	const char *datatype;
	struct {
		ffpcmex fmt;
		ffpcmex convfmt; //format of audio data produced by converter filter
		uint64 pos; //samples
		uint64 total; //samples
		uint64 seek; //msec
		fmed_apos until;
		fmed_apos abs_seek; //seek position from the beginning of file
		uint gain; //dB * 100
		float maxpeak; //dB
		uint bitrate; //bit/s
		const char *decoder;
	} audio;

	struct {
		ffstr profile;
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
	fftime mtime;
	union {
	uint bits;
	struct {
		uint input_info :1;
		uint out_preserve_date :1;
		uint out_overwrite :1;
		uint snd_output_clear :1;
		uint snd_output_pause :1;
		uint meta_changed :1;
		uint pcm_peaks :1;
		uint pcm_peaks_crc :1;
		uint out_seekable :1;
		uint meta_block :1; //data block isn't audio
		uint stream_copy :1;
		uint codec_err :1;
		uint mpg_lametag :1;
		uint out_file_del :1;
		uint save_trk :1;
		uint net_reconnect :1;
		uint use_dynanorm :1;
		uint duration_inaccurate :1;
		uint e_no_source :1; // error: no media source
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

	uint64 a_prebuffer; //msec
	float a_start_level; //dB
	float a_stop_level; //dB
	uint a_stop_level_time; //msec
	uint a_stop_level_mintime; //msec
	ffarr2 include_files; //ffstr[]
	ffarr2 exclude_files; //ffstr[]
};

enum FMED_R {
	FMED_ROK //output data is ready.  The module will be called again if there's unprocessed input data.
	, FMED_RDATA //output data is ready, the module will be called again
	, FMED_RDONE //output data is completed, remove this module from the chain
	, FMED_RLASTOUT //output data is completed, remove this & all previous modules from the chain

	, FMED_RMORE //more input data is needed
	, FMED_RBACK //same as FMED_RMORE, but pass the current output data to the previous filter

	, FMED_RASYNC //an asynchronous operation is scheduled.  The module will call fmed_filt.handler.
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

struct fmed_filter2 {
	void* (*open)(fmed_filt *d);
	int (*process)(void *ctx, fmed_filt *d);
	void (*close)(void *ctx);

	/** Send a command directly to a filter instance. */
	ssize_t (*cmd)(void *ctx, uint cmd, ...);
};

struct fmed_aconv {
	ffpcmex in, out;
};

static FFINL int64 fmed_popval_def(fmed_filt *d, const char *name, int64 def)
{
	int64 n;
	if (FMED_NULL != (n = d->track->popval(d->trk, name)))
		return n;
	return def;
}

enum FMED_OUTCP {
	FMED_OUTCP_ALL = 1,
	FMED_OUTCP_CMD,
};


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

#define fmed_dbglog(core, trk, mod, ...) \
do { \
	if ((core)->loglev == FMED_LOG_DEBUG) \
		(core)->log(FMED_LOG_DEBUG, trk, mod, __VA_ARGS__); \
} while (0)

#define fmed_infolog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_INFO, trk, mod, __VA_ARGS__)

#define fmed_warnlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_WARN, trk, mod, __VA_ARGS__)

#define fmed_syswarnlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_WARN | FMED_LOG_SYS, trk, mod, __VA_ARGS__)

#define fmed_errlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_ERR, trk, mod, __VA_ARGS__)

#define fmed_syserrlog(core, trk, mod, ...) \
	(core)->log(FMED_LOG_ERR | FMED_LOG_SYS, trk, mod, __VA_ARGS__)

//obsolete:
#define dbglog  fmed_dbglog
#define warnlog  fmed_warnlog
#define errlog  fmed_errlog
#define syserrlog  fmed_syserrlog

typedef struct fmed_logdata {
	uint64 tid;
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

	/** Start playing next/previous track.
	@param: fmed_que_entry* */
	FMED_QUE_NEXT2,
	FMED_QUE_PREV2,

	/** Save playlist to file. */
	FMED_QUE_SAVE,

	FMED_QUE_CLEAR,
	FMED_QUE_ADD,
	FMED_QUE_RM,
	FMED_QUE_RMDEAD,
	FMED_QUE_METASET, // @param2: ffstr name_val_pair[2]
	FMED_QUE_SETONCHANGE, // @param: fmed_que_onchange_t
	FMED_QUE_EXPAND, // @param: fmed_que_entry*
	FMED_QUE_HAVEUSERMETA, // @param: fmed_que_entry*

	FMED_QUE_NEW,
	FMED_QUE_DEL, // @param: uint
	FMED_QUE_SEL, // @param: uint
	FMED_QUE_LIST, // @param: fmed_que_entry*

	/** Return 1 if entry is inside the currently selected playlist.
	bool iscurlist(fmed_que_entry *ent) */
	FMED_QUE_ISCURLIST,

	/** size_t get_id(fmed_que_entry *ent) */
	FMED_QUE_ID,
	/** fmed_que_entry* item(size_t plid, size_t id) */
	FMED_QUE_ITEM,

	_FMED_QUE_LAST
};

enum FMED_QUE_CMDF {
	_FMED_QUE_FMASK = 0xffff0000,
	FMED_QUE_NO_ONCHANGE = 0x10000,
	FMED_QUE_ADD_DONE = 0x20000,
	FMED_QUE_COPY_PROPS = 0x40000, //copy track properties from fmed_que_entry.prev

	/* More items will follow until FMED_QUE_ADD | FMED_QUE_ADD_DONE is sent with param=NULL. */
	FMED_QUE_MORE = 0x080000,
};

enum FMED_QUE_META_F {
	FMED_QUE_TMETA = 1,
	FMED_QUE_OVWRITE = 2,
	FMED_QUE_UNIQ = 4,
	FMED_QUE_TRKDICT = 8,
	FMED_QUE_NUM = 0x10,
	FMED_QUE_METADEL = 0x20,
	FMED_QUE_NO_TMETA = 0x40, //don't include transient meta
	FMED_QUE_PRIV = 0x80, //private meta data
	FMED_QUE_ACQUIRE = 0x0100, //don't copy data, acquire value buffer
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

	/**
	@cmd: enum FMED_QUE + enum FMED_QUE_CMDF
	*/
	ssize_t (*cmdv)(uint cmd, ...);
} fmed_queue;

/** Get playlist entry.
plid: playlist ID. -1: current.
id: item ID
Return fmed_que_entry*. */
#define fmed_queue_item(plid, id)  cmdv(FMED_QUE_ITEM, (ssize_t)(plid), (size_t)(id))

#define fmed_queue_save(qid, filename) \
	cmdv(FMED_QUE_SAVE, (size_t)(qid), (void*)(filename))


// GLOBCMD

enum FMED_GLOBCMD {
	FMED_GLOBCMD_OPEN, //connect to another instance.  Arguments: "char *pipename"
	FMED_GLOBCMD_START, //listen for connections.  Arguments: "char *pipename"
};

typedef struct fmed_globcmd_iface {
	/**
	@cmd: enum FMED_GLOBCMD. */
	int (*ctl)(uint cmd, ...);
	int (*write)(const void *data, size_t len);
} fmed_globcmd_iface;


// NET

#include <FF/net/http.h>

typedef struct fmed_net_http {
	/** Create HTTP request.
	Return connection object. */
	void* (*request)(const char *method, const char *url, uint flags);

	/** Close connection. */
	void (*close)(void *con);

	/** Set asynchronous callback function.
	User function is called every time the connection status changes. */
	void (*sethandler)(void *con, fftask_handler func, void *udata);

	/** Connect, send request, receive response. */
	void (*send)(void *con, const ffstr *data);

	/** Get response data.
	@data: response body
	Return enum FMED_NET_ST. */
	int (*recv)(void *con, ffhttp_response **resp, ffstr *data);
} fmed_net_http;

enum FMED_NET_ST {
	FMED_NET_ERR = -1,
	FMED_NET_DONE = 0,
	FMED_NET_DNS_WAIT, // resolving hostname via DNS
	FMED_NET_IP_WAIT, // connecting to host
	FMED_NET_REQ_WAIT, // sending request
	FMED_NET_RESP_WAIT, // receiving response (HTTP headers)
	FMED_NET_RESP_RECV, // receiving data
};
