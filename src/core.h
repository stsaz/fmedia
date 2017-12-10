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
#include <FFOS/asyncio.h>
#include <FFOS/file.h>
#include <FFOS/process.h>


typedef struct fmed_config {
	byte codepage;
	byte instance_mode;
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
	fftaskmgr taskmgr;
	ffkevent evposted;

	fftimer_queue tmrq;
	uint period;

	uint trkid;
	fflist trks; //fm_trk[]

	fffd kq;
	ffkqu_time kqutime;
	ffkevpost kqpost;

	uint stopped :1
		, stop_sig :1
		;

	ffarr bmods; //fmed_modinfo[]
	fflist mods; //core_mod[]

	ffenv env;
	ffstr root;
	fmed_config conf;
	fmed_cmd cmd;
	fmed_props props;

	const fmed_queue *qu;

#ifdef FF_WIN
	ffwoh *woh;
#endif
} fmedia;

extern fmedia *fmed;
extern fmed_core *core;
extern const fmed_track _fmed_track;
