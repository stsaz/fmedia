/** M3U, PLS input.
Copyright (c) 2015 Simon Zolin */

#include <fmedia.h>
#include <util/path.h>
#include <util/url.h>

const fmed_core *core;
const fmed_queue *qu;


//FMEDIA MODULE
static const void* plist_iface(const char *name);
static int plist_sig(uint signo);
static void plist_destroy(void);
static int plist_conf(const char *name, fmed_conf_ctx *ctx);
static const fmed_mod fmed_plist_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&plist_iface, &plist_sig, &plist_destroy, &plist_conf
};

typedef struct pls_entry {
	ffarr url;
	ffarr artist;
	ffarr title;
	int duration;
	uint clear :1;
} pls_entry;

static FFINL void pls_entry_free(pls_entry *ent)
{
	ffarr_free(&ent->url);
	ffarr_free(&ent->artist);
	ffarr_free(&ent->title);
	ent->duration = -1;
}

int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_plist_mod;
}

extern const fmed_filter fmed_cue_input;
extern const fmed_filter cuehook_iface;
extern const fmed_filter fmed_dir_input;
extern int dir_conf(fmed_conf_ctx *ctx);

#include <format/m3u-read.h>
#include <format/pls-read.h>
#include <format/m3u-write.h>

static const void* plist_iface(const char *name)
{
	if (!ffsz_cmp(name, "m3u"))
		return &fmed_m3u_input;
	else if (ffsz_eq(name, "m3u-out"))
		return &m3u_output;
	else if (!ffsz_cmp(name, "pls"))
		return &fmed_pls_input;
	else if (!ffsz_cmp(name, "cue"))
		return &fmed_cue_input;
	else if (ffsz_eq(name, "cuehook"))
		return &cuehook_iface;
	else if (!ffsz_cmp(name, "dir"))
		return &fmed_dir_input;
	return NULL;
}

static int plist_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (!ffsz_cmp(name, "dir"))
		return dir_conf(ctx);
	return -1;
}

static int plist_sig(uint signo)
{
	switch (signo) {
	case FMED_OPEN:
		if (NULL == (qu = core->getmod("#queue.queue")))
			return 1;
		break;
	}
	return 0;
}

static void plist_destroy(void)
{
}


/** Parse URI scheme.
Return scheme length on success. */
static inline uint ffuri_scheme(const char *s, size_t len)
{
	ffstr scheme;
	if (0 >= (ssize_t)ffs_fmatch(s, len, "%S://", &scheme))
		return 0;
	return scheme.len;
}

/** Get absolute filename. */
int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst)
{
	const char *fn;
	ffstr path = {0};
	ffstr3 s = {0};

	if (0 == ffuri_scheme(name->ptr, name->len)
		&& !ffpath_abs(name->ptr, name->len)) {

		fn = d->track->getvalstr(d->trk, "input");
		if (NULL != ffpath_split2(fn, ffsz_len(fn), &path, NULL))
			path.len++;
	}

	if (0 == ffstr_catfmt(&s, "%S%S", &path, name))
		return 1;
	ffstr_acqstr3(dst, &s);
	return 0;
}
