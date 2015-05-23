/**
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/rbtree.h>
#include <FF/list.h>
#include <FF/taskqueue.h>
#include <FFOS/asyncio.h>
#include <FFOS/file.h>


typedef struct fm_src fm_src;

typedef struct inmap_item {
	const fmed_modinfo *mod;
	char ext[0];
} inmap_item;

typedef struct fmedia {
	fftaskmgr taskmgr;
	ffkevent evposted;

	uint in_files_cur;
	uint srcid;
	fflist srcs; //fm_src[]

	fm_src *dst;

	fffd kq;
	const ffkqu_time *pkqutime;
	ffkqu_time kqutime;

	unsigned playing :1
		, recording :1
		, stopped :1
		;

	fmed_core core;
	struct { FFARR(fmed_modinfo) } mods;

	//conf:
	ffstr root;
	ffarr in_files;
	ffstr rec_file
		, outfn;
	uint playdev_name
		, captdev_name;
	uint seek_time
		, until_time;
	uint64 fseek;
	const fmed_modinfo *output;

	const fmed_modinfo *input;
	ffpcm inp_pcm;

	ffbool repeat_all
		, debug
		, mix
		, silent
		, info;
	ffstr3 inmap; //inmap_item[]
	const fmed_modinfo *inmap_curmod;
} fmedia;

extern fmedia *fmed;
extern fmed_core *core;

extern int core_init(void);
extern void core_free(void);

extern void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);
