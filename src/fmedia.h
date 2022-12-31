/** fmedia: public interfaces for inter-module communication
Copyright (c) 2015 Simon Zolin */

#pragma once
#include <util/ffos-compat/types.h>
#include <util/array.h>
#include <util/conf2-scheme.h>
#include <util/taskqueue.h>
#include <util/ffos-compat/asyncio.h>
#include <afilter/pcm.h>
#include <FFOS/file.h>
#include <FFOS/error.h>
#include <FFOS/timerqueue.h>

#define FMED_VER_MAJOR  1
#define FMED_VER_MINOR  29
#define FMED_VER_PATCH  1
#define FMED_VER_FULL  ((FMED_VER_MAJOR << 16) | (FMED_VER_MINOR << 8) | FMED_VER_PATCH)

/** Inter-module compatibility version.
It must be updated when incompatible changes are made to this file,
 then all modules must be rebuilt.
The core will refuse to load modules built for any other core version. */
#define FMED_VER_CORE  ((FMED_VER_MAJOR << 16) | (31<<8))

#define FMED_HOMEPAGE  "https://stsaz.github.io/fmedia/"

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
typedef void fmed_track_obj;

#define FMED_MODFUNCNAME  "fmed_getmod" //name of the function which is exported by a module
typedef const fmed_mod* (*fmed_getmod_t)(const fmed_core *core);

enum FMED_CMD {
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

	/** Remove a cross-worker task.
	void task_xdel(fftask *task, uint wid) */
	FMED_TASK_XDEL,

	/** Assign command to worker.  Must be called on main thread.
	uint assign(fffd *kq, uint flags)
	flags: enum FMED_WORKER_F
	Return worker ID. */
	FMED_WORKER_ASSIGN,

	/** Release command from worker.
	void release(uint wid, uint flags)
	flags: enum FMED_WORKER_F
	*/
	FMED_WORKER_RELEASE,

	/** Get the number of available workers.
	Worker is considered to be busy only if it has a command with FMED_WORKER_FPARALLEL.
	Return 0: workers are busy;  1: at least 1 worker is free. */
	FMED_WORKER_AVAIL,

	/** Set logger.
	void setlog(const fmed_log *log)
	*/
	FMED_SETLOG,

	/** Get local time zone offset.
	uint tzoff() */
	FMED_TZOFFSET,

	/**
	args: "ffstr *file_extension"
	Return enum FMED_FT. */
	FMED_FILETYPE_EXT,

	/** Guess file format by data
	args: "ffstr *data"
	Return char* - file extension
	 NULL: unknown */
	FMED_DATA_FORMAT,

	/** Get input filter name by file extension.
	const char *fname = (char*)core->cmd(FMED_IFILTER_BYEXT, const char *ext) */
	FMED_IFILTER_BYEXT,

	/** Get output filter name by file extension.
	const char *fname = (char*)core->cmd(FMED_OFILTER_BYEXT, const char *ext) */
	FMED_OFILTER_BYEXT,

	/** Get filter interface by name.
	const fmed_filter *fi = (fmed_filter*)core->cmd(FMED_FILTER_BYNAME, const char *name) */
	FMED_FILTER_BYNAME,
};

enum FMED_WORKER_F {
	FMED_WORKER_FPARALLEL = 1,
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
	/** Get so/dll file context (load module if necessary)
	'name': so/dll file name without extension
	*/
	FMED_MOD_SOINFO = 1,
	FMED_MOD_IFACE, /** Get module's interface (configured only). */

	/** Get fmed_modinfo* by input/output file extension. */
	FMED_MOD_INEXT, // obsolete
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
	void (*log)(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, ...);
	void (*logv)(uint flags, fmed_track_obj *trk, const char *module, const char *fmt, va_list va);

	/** Return NULL on error. */
	char* (*getpath)(const char *name, size_t len);

	char* (*env_expand)(char *dst, size_t cap, const char *src);

	int (*sig)(uint signo); // obsolete

	/**
	cmd: enum FMED_CMD. */
	ssize_t (*cmd)(uint cmd, ...);

	/** Get module interface. */
	const void* (*getmod)(const char *name);

	/**
	@flags: enum FMED_GETMOD.
	*/
	const void* (*getmod2)(uint flags, const char *name, ssize_t name_len);

	const fmed_modinfo* (*insmod)(const char *name, ffconf_scheme *fc);

	/** Add task to the main worker.
	@cmd: enum FMED_TASK. */
	void (*task)(fftask *task, uint cmd);

	/** Set timer on the main worker.
	@interval:  >0: periodic;  <0: one-shot;  0: disable.
	Return 0 on success. */
	int (*timer)(fftimerqueue_node *tmr, int64 interval, uint flags);
};

static inline void fmed_timer_set(fftimerqueue_node *t, fftimerqueue_func func, void *param)
{
	t->func = func;
	t->param = param;
}

enum FMED_INSTANCE_MODE {
	FMED_IM_OFF,
	FMED_IM_ADD,
	FMED_IM_PLAY,
	FMED_IM_CLEARPLAY,
};

struct fmed_props {
	uint stdout_busy :1;
	uint stdin_busy :1;
	uint parallel :1;
	uint prevent_sleep :1;
	uint gui :1; // GUI is enabled
	uint tui :1; // TUI is enabled
	char *version_str; // "X.XX[.XX]"

	/** Path to user configuration directory (with the trailing slash).
	Portable mode: "{FMEDIA_DIR}/"
	Windows: "%APPDATA%/fmedia/"
	Linux:   "$HOME/.config/fmedia/" */
	char *user_path;

	const fmed_modinfo *playback_module;
	uint playback_dev_index;
	const fmed_modinfo *record_module;
	ffpcm record_format;

	char language[8];
	uint codepage;
};

typedef ffconf_arg fmed_conf_arg;
typedef struct ffconf_schemectx fmed_conf_ctx;
typedef ffconf_scheme fmed_conf;
#define fmed_conf_addnewctx(fc, o, a)  ffconf_scheme_addctx(fc, a, o)
#define fmed_conf_addctx(cx, o, a) \
do { \
	(cx)->args = a; \
	(cx)->obj = o; \
} while (0)
#define fmed_conf_skipctx(c)  ffconf_scheme_skipctx(c)
#define fmed_conf_any_keyname(c)  ffconf_scheme_keyname(c)
#define FMC_O(s, m)  FF_OFF(s, m)
#define FMC_F(f)  (ffsize)(f)
#define FMC_STR  FFCONF_TSTR
#define FMC_STR_MULTI  FFCONF_TSTR | FFCONF_FMULTI
#define FMC_STRNE  FFCONF_TSTR | FFCONF_FNOTEMPTY
#define FMC_STRZ  FFCONF_TSTRZ
#define FMC_STRZNE  FFCONF_TSTRZ | FFCONF_FNOTEMPTY
#define FMC_STRZ_LIST  FFCONF_TSTRZ | FFCONF_FLIST
#define FMC_INT8  FFCONF_TINT8
#define FMC_INT8NZ  FFCONF_TINT8 | FFCONF_FNOTZERO
#define FMC_INT16  FFCONF_TINT16
#define FMC_INT16_LIST  FFCONF_TINT16 | FFCONF_FLIST
#define FMC_INT32  FFCONF_TINT32
#define FMC_INT32NZ  FFCONF_TINT32 | FFCONF_FNOTZERO
#ifdef FF_64
	#define FMC_SIZE  FFCONF_TSIZE64
	#define FMC_SIZENZ  FFCONF_TSIZE64 | FFCONF_FNOTZERO
#else
	#define FMC_SIZE  FFCONF_TSIZE32
	#define FMC_SIZENZ  FFCONF_TSIZE32 | FFCONF_FNOTZERO
#endif
#define FMC_BOOL8  FFCONF_TBOOL8
#define FMC_FLOAT32S  FFCONF_TFLOAT32 | FFCONF_FSIGN
#define FMC_FLOAT64S  FFCONF_TFLOAT64 | FFCONF_FSIGN
#define FMC_ONCLOSE  FFCONF_TCLOSE
#define FMC_OBJ  FFCONF_TOBJ
#define FMC_EBADVAL  FFCONF_EBADVAL
#define FMC_ESYS  FFCONF_ESYS

struct fmed_mod {
	uint ver;
	uint ver_core;

	const void* (*iface)(const char *name);

	/**
	signo: enum FMED_CMD. */
	int (*sig)(uint signo);

	void (*destroy)(void);

	int (*conf)(const char *name, fmed_conf_ctx *ctx);
};


// TRACK

#define FMED_NULL (-1LL)
#define FMED_PNULL ((void*)(ffssize)-1)

enum FMED_TRACK_CMD {
	FMED_TRACK_START,
	FMED_TRACK_STOP,

	/** Pause/unpause tracks
	trk: -1: (un)pause all */
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
	FMED_TRACK_FILT_ADDFIRST,
	FMED_TRACK_FILT_ADDLAST,

	/** Return 1 if have meta data from user */
	FMED_TRACK_META_HAVEUSER,

	/** Get next meta key-value pair.
		fmed_trk_meta meta = {};
		// meta.flags = FMED_QUE_UNIQ;
		int r = track->cmd(trk, FMED_TRACK_META_ENUM, &meta);
		// use meta.name & meta.val
	Return
	  0: have key-value pair
	  !=0: done */
	FMED_TRACK_META_ENUM,

	/**
	@param: void *src_track */
	FMED_TRACK_META_COPYFROM,

	FMED_TRACK_FILT_GETPREV, // get context pointer of the previous filter
	FMED_TRACK_FILT_INSTANCE, // get (create) filter instance.  @param: void *filter_id

	/** Continue track processing after suspend and an asynchronous event. */
	FMED_TRACK_WAKE,

	FMED_TRACK_MONITOR,

	/** Get kernel queue associated with this track.
	Return fffd. */
	FMED_TRACK_KQ,

	/** Start a track in any worker. */
	FMED_TRACK_XSTART,

	/** Mark the track as stopped (as if user has pressed Stop button).
	'queue' module won't start the next track. */
	FMED_TRACK_STOPPED,
};

enum FMED_TRK_TYPE {
	FMED_TRK_TYPE_NONE,
	FMED_TRK_TYPE_PLAYBACK,
	FMED_TRK_TYPE_REC,
	FMED_TRK_TYPE_MIXIN,
	FMED_TRK_TYPE_MIXOUT,
	FMED_TRK_TYPE_NETIN,
	FMED_TRK_TYPE_EXPAND, // get file meta info
	FMED_TRK_TYPE_PLIST, // write playlist file from queue
	FMED_TRK_TYPE_PCMINFO, // analyze PCM peaks and CRC

	/** Write output to a file.
	Set file name with 'fmed_track_info.out_filename'. */
	FMED_TRK_TYPE_CONVERT,
	/** Just print meta data */
	FMED_TRK_TYPE_METAINFO,

	_FMED_TRK_TYPE_END,
};

enum FMED_TRK_FVAL {
	FMED_TRK_FACQUIRE = 2, //acquire pointer (value will be deleted with ffmem_free())
	FMED_TRK_NAMESTR = 8,
	FMED_TRK_VALSTR = 0x10,
	FMED_TRK_META = 0x20,
};

#define FMED_TRK_ETMP  NULL // transient/system error
#define FMED_TRK_EFMT  ((void*)-1) // format is unsupported

typedef struct fmed_track_info fmed_track_info;
typedef struct fmed_track_info fmed_trk; // obsolete
typedef struct fmed_track_info fmed_filt; // obsolete

typedef struct fmed_track {
	/**
	@cmd: enum FMED_TRK_TYPE.
	Return track ID;  FMED_TRK_E* on error. */
	fmed_track_obj* (*create)(uint cmd, const char *url);

	fmed_track_info* (*conf)(fmed_track_obj *trk);

	void (*copy_info)(fmed_track_info *dst, const fmed_track_info *src);

	/**
	@cmd: enum FMED_TRACK_CMD. */
	ssize_t (*cmd)(fmed_track_obj *trk, uint cmd, ...);

	// obsolete
	int (*cmd2)(fmed_track_obj *trk, uint cmd, void *param);

	int64 (*popval)(fmed_track_obj *trk, const char *name);

	/** Return FMED_NULL on error. */
	int64 (*getval)(fmed_track_obj *trk, const char *name);

	/** Return FMED_PNULL on error. */
	const char* (*getvalstr)(fmed_track_obj *trk, const char *name);

	int (*setval)(fmed_track_obj *trk, const char *name, int64 val);

	int (*setvalstr)(fmed_track_obj *trk, const char *name, const char *val);

	/**
	@flags: enum FMED_TRK_FVAL */
	int64 (*setval4)(fmed_track_obj *trk, const char *name, int64 val, uint flags);
	char* (*setvalstr4)(fmed_track_obj *trk, const char *name, const char *val, uint flags);

	char* (*getvalstr3)(fmed_track_obj *trk, const void *name, uint flags);

	void (*loginfo)(fmed_track_obj *trk, const ffstr **id, const char **module);

	/**
	@flags: enum FMED_QUE_META_F */
	void (*meta_set)(fmed_track_obj *trk, const ffstr *name, const ffstr *val, uint flags);
} fmed_track;

#define fmed_getval(name)  (d)->track->getval((d)->trk, name)
#define fmed_popval(name)  (d)->track->popval((d)->trk, name)
#define fmed_setval(name, val)  (d)->track->setval((d)->trk, name, val)

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
	/** Called within the worker thread when track object is about to be destroyed. */
	void (*onsig)(fmed_track_obj *trk, uint sig);
};
/** Associate monitor interface with tracks. */
#define fmed_trk_monitor(trk, mon)  cmd(NULL, FMED_TRACK_MONITOR, mon)


// FILTER

typedef void (*fmed_handler)(void *udata);

enum FMED_F {
	FMED_FLAST = 1, // the last chunk of input data (obsolete)
	FMED_FFIRST = 1, // filter is first in chain
	FMED_FSTOP = 2, // track is being stopped
	FMED_FFWD = 4,
};

enum FMED_E {
	FMED_E_SUCCESS,
	FMED_E_NOSRC, // no source
	FMED_E_DSTEXIST, // target exists already
	FMED_E_UNKIFMT, // unknown input format
	FMED_E_INCOMPATFMT, // incompatible data formats
	FMED_E_OTHER = 255,
	FMED_E_SYS = 0x80000000,
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

struct fmed_adev;
struct fmed_track_info {
	const fmed_track *track;
	fmed_handler handler;
	fmed_track_obj *trk;

	uint flags; //enum FMED_F

	/** enum FMED_E
	Filter sets it when it encounters an error and returns FMED_RERR.
	'track' module sets it to FMED_E_OTHER for all other error cases. */
	uint error;

	uint type; //enum FMED_TRK_TYPE
	const char *datatype;
	struct {
		/** Current audio format.
		Initially set from fmedia.conf::record_format{} (for recording tracks).
		Updated by command line: --format/--rate/--channels.
		Audio capture modules update it according to the actually used format.
		Demuxers/decoders set it according to the file format.
		*/
		ffpcmex fmt;

		/** Audio format produced by converter filter.
		Initially a copy of 'fmt'.
		Encoders and audio playback modules use it and update it when they request for audio conversion. */
		ffpcmex convfmt;

		uint64 pos; //samples
		uint64 total; //total track length (samples); -1:unset;  0:streaming
		/** Seek position (msec).  See tech.md::Seeking.  -1:unset. */
		uint64 seek;
		fmed_apos until;
		uint64 split;
		fmed_apos abs_seek; //seek position from the beginning of file
		/** Audio gain/attenuation (dynamic) */
		int gain; //dB * 100
		/** Signal ceiling (dB) for auto-attenuator (static) */
		float auto_attenuate_ceiling;
		float maxpeak; //dB
		uint bitrate; //bit/s
		const char *decoder;
	} audio;
	struct {
		uint width, height;
		const char *decoder;
	} video;
	// afilter.conv settings (Note: reused each time afilter.conv is added)
	struct {
		ffpcmex in, out;
	} aconv;
	uint a_prebuffer; //msec
	float a_start_level; //dB
	float a_stop_level; //dB
	uint a_stop_level_time; //msec
	uint a_stop_level_mintime; //msec
	ushort a_in_buf_time; // buffer size for audio input (msec)  0:default
	uint a_enc_delay;
	uint a_end_padding;
	uint a_frame_samples;
	uint a_enc_bitrate; // bit/sec
	const char *in_filename;
	fftime in_mtime;
	/** Output file name.
	core free()s it automatically when track is destroyed.
	fmed_track.copy_info() frees the current pointer and copies the data from source into a new region. */
	char *out_filename;
	fftime out_mtime;
	/** net.in sets out_filename from this. */
	const char *net_out_filename;

	ffvec meta; // {char*, char*}[]

	fftime mtime; // obsolete

	ffslice include_files; //ffstr[]
	ffslice exclude_files; //ffstr[]

	/** stream_copy=1: ogg.read -> ogg.write: granule-position value from source */
	uint64 ogg_granule_pos;

	ushort mpeg1_delay;
	ushort mpeg1_padding;
	byte mpeg1_vbr_scale; // +1

	// flac.read -> flac.dec:
	uint flac_samples;
	uint flac_minblock, flac_maxblock;
	const char *flac_vendor; // flac.enc -> flac.write
	uint flac_frame_samples; // flac.enc -> flac.write

	/** UI -> adev.out fast signal delivery (e.g. for fast reaction to seek command). */
	const struct fmed_adev *adev;
	void *adev_ctx;

	// the region is initially filled with 0xff until '_bar_end'
	byte _bar_start;
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
		signed char gaps;
	} cue;

	struct {
		uint64 size;
		uint64 seek;
	} input, output;
	byte _bar_end;

	union {
	uint bits;
	struct {
		uint input_info :1;
		uint out_preserve_date :1;
		uint out_overwrite :1;
		/** net.in -> file.out: delete file on close */
		uint out_file_del :1;
		uint out_seekable :1;
		/** Seek request is received.
		Demuxer clears it. */
		uint seek_req :1;
		uint snd_output_pause :1;
		uint meta_changed :1;
		uint meta_block :1; //data block isn't audio
		uint pcm_peaks :1;
		uint pcm_peaks_crc :1;
		uint stream_copy :1;
		/** net.in sets 'stream_copy' */
		uint net_stream_copy :1;
		/** net.icy creates a new track for net.in.  enum FMED_OUTCP. */
		uint net_out_copy :2;
		uint save_trk :1;
		uint net_reconnect :1;
		uint use_dynanorm :1;
		uint _obsolete :1;
		uint err :1; // obsolete
		uint show_tags :1;
		uint print_time :1;
		uint duration_accurate :1;
		/** mpeg.enc -> mp3.write: this packet is LAME frame */
		uint mpg_lametag :1;
		uint ogg_flush :1;
		uint ogg_gen_opus_tag :1; // ogg.write must generate Opus-tag packet

		uint reserve :6;
	};
	};

	union {
		struct {
			ffstr data_in;
			ffstr data_out;
		};
		// obsolete:
		struct {
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
	};
};

enum FMED_R {
	FMED_ROK //output data is ready.  The module will be called again if there's unprocessed input data.
	, FMED_RDATA //output data is ready, the module will be called again

	/** Output data is completed, remove this module from the chain.

	1. [... -> current -- next...]
	2. [...            -> next...]
	3. current.close()
	4. [... <- next... ]
	*/
	, FMED_RDONE
	, FMED_RLASTOUT //output data is completed, remove this & all previous modules from the chain

	/** Output for the next filters is completed:
	. pass output data to the next filter
	. close all next filters (via FMED_FLAST)
	. return to this filter

	1. [... -> current -- next...]
	2. [               -> (close) next...]
	3. [... -- current <- ]
	*/
	, FMED_RNEXTDONE

	, FMED_RMORE //more input data is needed
	, FMED_RBACK //same as FMED_RMORE, but pass the current output data to the previous filter

	, FMED_RASYNC //an asynchronous operation is scheduled.  The module will call fmed_filt.handler.
	, FMED_RFIN //close the track
	, FMED_RSYSERR //system error.  Print error message and close the track.
	, FMED_RDONE_ERR // same as FMED_RDONE but because of an error
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
	FMED_OUTCP_ALL = 1, // --out-copy
	FMED_OUTCP_CMD, // --out-copy-cmd
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
#define fmed_dbglogv(core, trk, mod, fmt, va) \
do { \
	if ((core)->loglev == FMED_LOG_DEBUG) \
		(core)->logv(FMED_LOG_DEBUG, trk, mod, fmt, va); \
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
	fmed_track_obj *trk;
} fmed_logdata;

typedef struct fmed_log {
	void (*log)(uint flags, fmed_logdata *ld);
} fmed_log;


// ADEV

enum FMED_ADEV_F {
	FMED_ADEV_PLAYBACK,
	FMED_ADEV_CAPTURE,
};

enum FMED_ADEV_CMD {
	/** Force stop() and clear() on playback audio buffer. */
	FMED_ADEV_CMD_CLEAR = 1,
};

typedef struct fmed_adev_ent {
	char *name;
	ffpcm default_format;
	uint default_device :1;
} fmed_adev_ent;

typedef struct fmed_adev {
	/**
	@flags: enum FMED_ADEV_F
	*/
	int (*list)(fmed_adev_ent **ents, uint flags);
	void (*listfree)(fmed_adev_ent *ents);
	/**
	cmd: FMED_ADEV_CMD */
	int (*cmd)(int cmd, void *adev_ctx);
} fmed_adev;


// QUEUE

/** Properties for an element in queue.
Can be passed into or returned from the queue module.

When an object is returned from queue module:
User must not use it out of the current function's scope.
The single worker thread indirectly protects the pointer from invalidation.
*/
typedef struct fmed_que_entry {
	ffstr url;
	int from // >0: msec;  <0: CD frames (1/75 sec)
		, to;
	union {
	int dur; //msec
	uint list_index;
	};
} fmed_que_entry;

enum FMED_QUE_EVT {
	FMED_QUE_ONADD,
	FMED_QUE_ONADD_DONE,
	FMED_QUE_ONRM,
	FMED_QUE_ONCLEAR,

	/** Notification after completion of the track which was started by FMED_QUE_EXPAND* command.  */
	FMED_QUE_ONUPDATE,
};

/** @flags: enum FMED_QUE_EVT [+ FMED_QUE_MORE]. */
typedef void (*fmed_que_onchange_t)(fmed_que_entry *e, uint flags);

enum FMED_QUE {
	/** Start playing track.
	@param: fmed_que_entry* */
	FMED_QUE_PLAY,
	/**
	Stop all active playback tracks */
	FMED_QUE_PLAY_EXCL,

	FMED_QUE_MIX,

	/** Don't autostart next entry in the playlist */
	FMED_QUE_STOP_AFTER,

	/** Start playing next/previous track.
	@param: fmed_que_entry*: base (current) track
	  NULL: base is the current track.  Stop the currently playing track automatically. */
	FMED_QUE_NEXT2,
	FMED_QUE_PREV2,

	/** Save playlist to file.
	int r = qu->cmdv(FMED_QUE_SAVE, int list_index, const char *filename);
	Return -1:plid doesn't exist */
	FMED_QUE_SAVE,

	/**
	qu->cmdv(FMED_QUE_CLEAR); */
	FMED_QUE_CLEAR,

	/** Add an item.
	@param: fmed_que_entry*
	Return fmed_que_entry*. */
	FMED_QUE_ADD,

	/** Remove an item.
	fmed_que_entry *e = ...;
	qu->cmdv(FMED_QUE_RM, fmed_que_entry *e); */
	FMED_QUE_RM,

	/** Remove "dead" items from the current playlist.
	Return N of removed items */
	FMED_QUE_RMDEAD,

	FMED_QUE_METASET, // @param2: ffstr name_val_pair[2]
	FMED_QUE_SETONCHANGE, // @param: fmed_que_onchange_t

	/** Expand (read meta data).
	void* expand(fmed_que_entry*)
	Return track object. */
	FMED_QUE_EXPAND,

	FMED_QUE_HAVEUSERMETA, // @param: fmed_que_entry*

	/** Create new list.
	flags: enum FMED_QUE_CMDF
	qu->cmdv(FMED_QUE_NEW, uint flags); */
	FMED_QUE_NEW,

	/** Delete a list.
	qu->cmdv(FMED_QUE_DEL, int list_index); */
	FMED_QUE_DEL,

	/**
	qu->cmdv(FMED_QUE_SEL, uint list_index); */
	FMED_QUE_SEL,

	/** List playlist entries.
	Return 0 if no more entries.
	fmed_que_entry *e = NULL;
	while (qu->cmdv(FMED_QUE_LIST, &e)) {
		...
	} */
	FMED_QUE_LIST,

	/** Return 1 if entry is inside the currently selected playlist.
	bool iscurlist(fmed_que_entry *ent) */
	FMED_QUE_ISCURLIST,

	/** Get item index.
	size_t get_id(fmed_que_entry *ent)
	Return -1 on error. */
	FMED_QUE_ID,

	/**
	fmed_que_entry *e = qu->cmdv(FMED_QUE_ITEM, int list_index, size_t id); */
	FMED_QUE_ITEM,

	/** Get an item and protect it from change.  Thread-safe.
	fmed_que_entry *e = qu->cmdv(FMED_QUE_ITEMLOCKED, int list_index, size_t id); */
	FMED_QUE_ITEMLOCKED,

	/** Unlock an item.  Thread-safe.
	void item_unlock(fmed_que_entry *ent) */
	FMED_QUE_ITEMUNLOCK,

	FMED_QUE_NEW_FILTERED,
	FMED_QUE_ADD_FILTERED,
	FMED_QUE_DEL_FILTERED,
	FMED_QUE_LIST_NOFILTER,

	/**
	void sort(int plist, const char *by, uint reverse)
	plist: list index or -1g
	by: meta name or "__dur" (duration) or "__url" or "__random"
	reverse: reverse order (0/1) */
	FMED_QUE_SORT,

	/**
	uint n = qu->cmdv(FMED_QUE_COUNT); */
	FMED_QUE_COUNT,

	/**
	uint n = qu->cmdv(FMED_QUE_COUNT2, int list_index); */
	FMED_QUE_COUNT2,

	/** Start processing several tracks in parallel, if possible.
	void xplay(fmed_que_entry *first)
	'first': the first track to start. */
	FMED_QUE_XPLAY,

	/** Add an item.
	fmed_que_entry e = {};
	ffstr_set(&e.url, ...);
	fmed_que_entry *qe = qu->cmdv(FMED_QUE_ADD2, int list_index, &e); */
	FMED_QUE_ADD2,

	/** Add an entry after another one.
	fmed_que_entry* addafter(fmed_que_entry *ent, const fmed_que_entry *after) */
	FMED_QUE_ADDAFTER,

	/** Associate track properties with a queue entry.
	void settrackprops(fmed_que_entry *ent, fmed_trk *trk) */
	FMED_QUE_SETTRACKPROPS,

	/** Copy track properties from another queue entry.
	void copytrackprops(fmed_que_entry *ent, const fmed_que_entry *src) */
	FMED_QUE_COPYTRACKPROPS,

	/** Play tracks randomly.  Thread-safe.
	uint random = VAL */
	FMED_QUE_SET_RANDOM,

	/**
	uint next_if_error = VAL */
	FMED_QUE_SET_NEXTIFERROR,

	/**
	uint repeat = enum FMED_QUE_REPEAT */
	FMED_QUE_SET_REPEATALL,

	/**
	uint quit_if_done = VAL */
	FMED_QUE_SET_QUITIFDONE,

	/** Expand source and notify user on completion.
	void* expand2(fmed_que_entry *e, void (*ondone)(void*), void *udata)
	Return track object. */
	FMED_QUE_EXPAND2,

	/** Expand all items in the current playlist */
	FMED_QUE_EXPAND_ALL,

	/** Get the current item index.
	Return 0 on error.
	size_t curid(int plist) */
	FMED_QUE_CURID,

	/** Set the current item index.
	void set_curid(int plist, size_t id) */
	FMED_QUE_SETCURID,

	/** Get N of lists
	uint n = qu->cmdv(FMED_QUE_N_LISTS); */
	FMED_QUE_N_LISTS,

	/**
	random_enabled = cmdv(FMED_QUE_FLIP_RANDOM) */
	FMED_QUE_FLIP_RANDOM,

	_FMED_QUE_LAST
};

enum FMED_QUE_REPEAT {
	FMED_QUE_REPEAT_NONE,
	FMED_QUE_REPEAT_ALL,
	FMED_QUE_REPEAT_TRACK,
};

enum FMED_QUE_CMDF {
	_FMED_QUE_FMASK = 0xffff0000,
	FMED_QUE_NO_ONCHANGE = 0x10000,
	FMED_QUE_ADD_DONE = 0x20000,

	/* More items will follow until FMED_QUE_ADD | FMED_QUE_ADD_DONE is sent with param=NULL. */
	FMED_QUE_MORE = 0x080000,

	/** FMED_QUE_NEW: Don't allow random play for this list. */
	FMED_QUE_NORND = 0x100000,
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
	FMED_QUE_NOLOCK = 0x0200, // don't try to lock playlist
};

#define FMED_QUE_SKIP  ((void*)-1)

typedef struct fmed_queue {
	/**
	@cmd: enum FMED_QUE + enum FMED_QUE_CMDF
	*/
	ssize_t (*cmdv)(uint cmd, ...);

	/** (obsolete)
	@cmd: enum FMED_QUE */
	void (*cmd)(uint cmd, void *param);

	/** (obsolete)
	@cmd: enum FMED_QUE + enum FMED_QUE_CMDF
	*/
	ssize_t (*cmd2)(uint cmd, void *param, size_t param2);

	fmed_que_entry* (*add)(fmed_que_entry *ent);

	/** flags: enum FMED_QUE_META_F */
	void (*meta_set)(fmed_que_entry *ent, const char *name, size_t name_len, const char *val, size_t val_len, uint flags);
	ffstr* (*meta_find)(fmed_que_entry *ent, const char *name, size_t name_len);

	/** Get meta.
	@flags: enum FMED_QUE_META_F.
	Return meta value
	 NULL: no more entries
	 FMED_QUE_SKIP: skip this entry */
	ffstr* (*meta)(fmed_que_entry *ent, size_t n, ffstr *name, uint flags);

	void (*meta_set2)(fmed_que_entry *ent, ffstr name, ffstr val, uint flags);
} fmed_queue;

/** Get playlist entry.
plid: playlist ID. -1: current.
id: item ID
Return fmed_que_entry*. */
#define fmed_queue_item(plid, id)  cmdv(FMED_QUE_ITEM, (int)(plid), (size_t)(id))

#define fmed_queue_item_locked(plid, id)  cmdv(FMED_QUE_ITEMLOCKED, (int)(plid), (size_t)(id))

#define fmed_queue_save(qid, filename) \
	cmdv(FMED_QUE_SAVE, (int)(qid), (void*)(filename))

#define fmed_queue_add(flags, plid, ent)  cmdv(FMED_QUE_ADD2 | (flags), (int)(plid), ent)


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

#include <util/http-client.h>

typedef struct fmed_net_http {
	/** Create HTTP request.
	flags: enum FFHTTPCL_F
	Return connection object. */
	void* (*request)(const char *method, const char *url, uint flags);

	/** Close connection. */
	void (*close)(void *con);

	/** Set asynchronous callback function.
	User function is called every time the connection status changes.
	Processing is suspended until user calls send(). */
	void (*sethandler)(void *con, ffhttpcl_handler func, void *udata);

	/** Connect, send request, receive response. */
	void (*send)(void *con, const ffstr *data);

	/** Get response data.
	@data: response body
	Return enum FFHTTPCL_ST. */
	int (*recv)(void *con, ffhttp_response **resp, ffstr *data);

	/** Add request header. */
	void (*header)(void *con, const ffstr *name, const ffstr *val, uint flags);

	/** Configure connection object.
	May be called only before the first send().
	flags: enum FFHTTPCL_CONF_F */
	void (*conf)(void *con, struct ffhttpcl_conf *conf, uint flags);
} fmed_net_http;


struct fmed_edittags_conf {
	const char *fn;
	ffstr meta, meta_from_filename;
	uint preserve_date :1;
};

typedef struct fmed_edittags {
	void (*edit)(struct fmed_edittags_conf *conf);
} fmed_edittags;
