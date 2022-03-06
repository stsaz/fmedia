/**
Copyright (c) 2019 Simon Zolin */

#include <core/core.h>
#include <util/conf-copy.h>
#include <FFOS/process.h>
#include <ffbase/map.h>


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
	ffmap in_ext_map, out_ext_map; // file extension -> inmap_item*

	ffconf_ctxcopy conf_copy;
	fmed_modinfo *conf_copy_mod; //core_mod

	int use_inmap :1;
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

const fmed_modinfo* core_insmod(ffstr name, ffconf_scheme *fc);
const fmed_modinfo* core_insmodz(const char *name, ffconf_scheme *fc);
const fmed_modinfo* core_insmod_delayed(ffstr sname);
const fmed_modinfo* core_getmodinfo(ffstr name);

enum CONF_F {
	CONF_F_USR = 1,
	CONF_F_OPT = 2,
};

/**
flags: enum CONF_F
*/
int core_conf_parse(fmed_config *conf, const char *filename, uint flags);
const fmed_modinfo* modbyext(const ffmap *map, const ffstr *ext);
int conf_init(fmed_config *conf);
void conf_destroy(fmed_config *conf);


#undef syserrlog
#define infolog0(...)  fmed_infolog(core, NULL, "core", __VA_ARGS__)
#define dbglog0(...)  fmed_dbglog(core, NULL, "core", __VA_ARGS__)
#define errlog0(...)  fmed_errlog(core, NULL, "core", __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)
#define syswarnlog(trk, ...)  fmed_syswarnlog(core, NULL, "core", __VA_ARGS__)
#define syserrlog(...)  fmed_syserrlog(core, NULL, "core", __VA_ARGS__)
