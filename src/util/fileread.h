/** (Asynchronous) file reader with userspace cache and read-ahead options.
Copyright (c) 2019 Simon Zolin
*/

#pragma once

#include "thpool.h"
#include "string.h"
#include <FFOS/file.h>


typedef struct fffileread fffileread;
enum FFFILEREAD_LOG {
	FFFILEREAD_LOG_DBG,
	FFFILEREAD_LOG_ERR,
	_FFFILEREAD_LOG_SYSERR,
};
typedef void (*fffileread_log)(void *udata, uint level, ffstr msg);
typedef void (*fffileread_onread)(void *udata);

typedef struct fffileread_conf {
	void *udata;
	fffileread_log log;
	fffileread_onread onread;
	ffthpool *thpool; // thread pool

	fffd kq; // kqueue descriptor
	uint oflags; // flags for fffile_open().  default:FFO_RDONLY

	uint bufsize; // size of 1 buffer.  Aligned to 'bufalign'.  default:64k
	uint nbufs; // number of buffers.  default:1
	uint bufalign; // buffer & file offset align value.  Power of 2.

	uint directio :1; // use direct I/O if available
	uint log_debug :1; // enable debug logging.  default:0
} fffileread_conf;

FF_EXTERN void fffileread_setconf(fffileread_conf *conf);

/** Create reader.
Return object pointer.
 conf.directio is set according to how file was opened
*/
FF_EXTERN fffileread* fffileread_create(const char *fn, fffileread_conf *conf);

/**
flags: 1:don't close fd
Release object (it may be freed later after the async task is complete). */
FF_EXTERN void fffileread_free_ex(fffileread *f, ffuint flags);
static inline void fffileread_free(fffileread *f)
{
	fffileread_free_ex(f, 0);
}

FF_EXTERN fffd fffileread_fd(fffileread *f);

enum FFFILEREAD_F {
	FFFILEREAD_FREADAHEAD = 1, // read-ahead: schedule reading of the next block
	FFFILEREAD_FBACKWARD = 2, // read-ahead: schedule reading of the previous block, not the next
	FFFILEREAD_FALLOWBLOCK = 4, // file reading is allowed to block this thread (i.e. perform synchronous I/O)
};

enum FFFILEREAD_R {
	FFFILEREAD_RREAD, // returning data
	FFFILEREAD_RERR, // error occurred
	FFFILEREAD_RASYNC, // asynchronous task is pending.  onread() will be called
	FFFILEREAD_REOF, // end of file is reached
};

/** Get data block from cache or begin reading data from file.
flags: enum FFFILEREAD_F
Return enum FFFILEREAD_R. */
FF_EXTERN int fffileread_getdata(fffileread *f, ffstr *dst, uint64 off, uint flags);

struct fffileread_stat {
	uint nread; // number of reads made
	uint nasync; // number of asynchronous requests
	uint ncached; // number of cache hits
};

FF_EXTERN void fffileread_stat(fffileread *f, struct fffileread_stat *st);
