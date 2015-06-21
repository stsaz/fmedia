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

typedef struct core_mod {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const fmed_filter *f;

	fflist_item sib;
} core_mod;

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
	fflist mods; //core_mod[]

	//conf:
	ffstr root;
	ffarr in_files;
	ffstr outfn
		, outdir;
	uint playdev_name
		, captdev_name;
	uint seek_time
		, until_time;
	uint64 fseek;
	const fmed_modinfo *output;

	const fmed_modinfo *input;
	ffpcm inp_pcm;

	ffbool repeat_all
		, overwrite
		, rec
		, debug
		, mix
		, silent
		, info;
	ffstr3 inmap; //inmap_item[]
	ffstr3 outmap; //inmap_item[]
	const fmed_modinfo *inmap_curmod;

	float ogg_qual;
	float gain;
	int conv_pcm_formt;
} fmedia;

extern fmedia *fmed;
extern fmed_core *core;

extern int core_init(void);
extern void core_free(void);

extern void fmed_log(fffd fd, const char *stime, const char *module, const char *level
	, const ffstr *id, const char *fmt, va_list va);
