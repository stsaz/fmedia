/**
Copyright (c) 2019 Simon Zolin */

#include <core/core.h>
#include <FF/data/conf.h>
#include <FFOS/process.h>


typedef struct fmed_config {
	byte codepage;
	byte instance_mode;
	byte prevent_sleep;
	byte workers;
	ffpcm inp_pcm;
	const fmed_modinfo *output;
	const fmed_modinfo *input;
	ffvec inmap; //inmap_item[]
	ffvec outmap; //inmap_item[]
	char *usrconf_modname;
	uint skip_line;

	ffconf_ctxcopy conf_copy;
	fmed_modinfo *conf_copy_mod; //core_mod

	const fmed_modinfo *inmap_curmod;
	ffvec *inoutmap;
} fmed_config;

typedef struct inmap_item {
	const fmed_modinfo *mod;
	char ext[0];
} inmap_item;

typedef struct core_mod {
	//fmed_modinfo:
	char *name;
	void *dl; //ffdl
	const fmed_mod *m;
	const void *iface;

	fflock lock;
	fmed_conf_ctx conf_ctx;
	ffstr conf_data;
	fflist_item sib;
	uint have_conf :1; // whether a module has configuration context
	char name_s[0];
} core_mod;

const fmed_modinfo* core_insmod_delayed(const char *sname, fmed_conf_ctx *ctx);
const fmed_modinfo* core_getmodinfo(const ffstr *name);

enum CONF_F {
	CONF_F_USR = 1,
	CONF_F_OPT = 2,
};

/**
flags: enum CONF_F
*/
int core_conf_parse(fmed_config *conf, const char *filename, uint flags);


#undef syserrlog
#define infolog0(...)  fmed_infolog(core, NULL, "core", __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, "core", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "core", __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define syswarnlog(trk, ...)  fmed_syswarnlog(core, NULL, "core", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "core", __VA_ARGS__)
