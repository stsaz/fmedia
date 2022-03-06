/** File writer with prebuffer.
Copyright (c) 2020 Simon Zolin
*/

#pragma once

#include "thpool.h"
#include "string.h"
#include <FFOS/file.h>
#include <FFOS/mem.h>


typedef struct fffilewrite fffilewrite;
enum FFFILEWRITE_LOG {
	FFFILEWRITE_LOG_DBG,
	FFFILEWRITE_LOG_ERR,
	_FFFILEWRITE_LOG_SYSERR,
};
typedef void (*fffilewrite_log)(void *udata, uint level, ffstr msg);
typedef void (*fffilewrite_onwrite)(void *udata);

typedef struct {
	void *udata;
	fffilewrite_log log;
	fffilewrite_onwrite onwrite;
	ffthpool *thpool; // thread pool

	uint oflags; // additional flags for fffile_open()
	fffd kq;
	uint bufsize; // buffer size.  default:64k
	uint nbufs; // number of buffers.  default:2
	uint align; // buffer align.  default:4k
	uint64 prealloc; // preallocate-by value (or total file size if known in advance).  default:128k
	uint prealloc_grow :1; // increase 'prealloc' value x2 on each preallocation.  default:1
	uint create :1; // create file. default:1
	uint overwrite :1; // overwrite existing file
	uint mkpath :1; // create full path.  default:1
	uint del_on_err :1; // delete the file if writing is incomplete
	// uint directio :1; // use O_DIRECT (if available)
	uint log_debug :1; // enable debug logging.  default:0
} fffilewrite_conf;

FF_EXTERN void fffilewrite_setconf(fffilewrite_conf *conf);

/** Create file writer.
Return object pointer */
FF_EXTERN fffilewrite* fffilewrite_create(const char *fn, fffilewrite_conf *conf);

/** Release object (it may be freed later after the async task is complete). */
FF_EXTERN void fffilewrite_free(fffilewrite *f);

enum FFFILEWRITE_R {
	FFFILEWRITE_RERR = -1, // error occurred
	FFFILEWRITE_RASYNC = -2, // asynchronous task is pending.  onwrite() will be called
};

enum FFFILEWRITE_F {
	FFFILEWRITE_FFLUSH = 1, // write bufferred data
};

/** Write data chunk to a file.
off: file offset
 -1: write to the current offset
flags: enum FFFILEWRITE_F
Return N of bytes written or enum FFFILEWRITE_R. */
FF_EXTERN ssize_t fffilewrite_write(fffilewrite *f, ffstr data, int64 off, uint flags);

/** Get file descriptor. */
FF_EXTERN fffd fffilewrite_fd(fffilewrite *f);

typedef struct {
	uint nmwrite; // N of memory writes
	uint nfwrite; // N of file writes
	uint nprealloc; // N of preallocations made
	uint nasync; // N of asynchronous requests
} fffilewrite_stat;

FF_EXTERN void fffilewrite_getstat(fffilewrite *f, fffilewrite_stat *stat);
