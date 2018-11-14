/**
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <core-cmd.h>

#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FF/rbtree.h>
#include <FF/list.h>
#include <FF/sys/taskqueue.h>
#ifdef FF_WIN
#include <FF/sys/wohandler.h>
#endif
#include <FF/data/conf.h>
#include <FFOS/asyncio.h>
#include <FFOS/file.h>
#include <FFOS/process.h>


typedef struct fmed_config {
	byte codepage;
	byte instance_mode;
	byte prevent_sleep;
	byte workers;
	ffpcm inp_pcm;
	const fmed_modinfo *output;
	const fmed_modinfo *input;
	ffstr3 inmap; //inmap_item[]
	ffstr3 outmap; //inmap_item[]
	const fmed_modinfo *inmap_curmod;
	char *usrconf_modname;
	uint skip_line :1;
} fmed_config;

typedef struct fmedia {
	ffarr workers; //worker[]
	ffkqu_time kqutime;

	uint stopped;

	ffarr bmods; //core_modinfo[]
	fflist mods; //core_mod[]

	ffenv env;
	ffstr root;
	fmed_config conf;
	fmed_cmd cmd;
	fmed_props props;

	const fmed_queue *qu;

	ffconf_ctxcopy conf_copy;
	fmed_modinfo *conf_copy_mod; //core_mod

#ifdef FF_WIN
	ffwoh *woh;
#endif
} fmedia;

extern fmedia *fmed;
extern fmed_core *core;
extern const fmed_track _fmed_track;


extern void core_job_enter(uint id, size_t *ctx);

extern ffbool core_job_shouldyield(uint id, size_t *ctx);

extern ffbool core_ismainthr(void);
