/** Directory input.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>

#include <FF/path.h>
#include <FF/sys/dir.h>


extern const fmed_core *core;
extern const fmed_queue *qu;
extern int plist_fullname(fmed_filt *d, const ffstr *name, ffstr *dst);

//DIR INPUT
static void* dir_open(fmed_filt *d);
static void dir_close(void *ctx);
static int dir_process(void *ctx, fmed_filt *d);
const fmed_filter fmed_dir_input = {
	&dir_open, &dir_process, &dir_close
};

int dir_conf(fmed_conf_ctx *ctx);
static int dir_open_r(const char *dirname, fmed_filt *d);

typedef struct dirconf_t {
	byte expand;
} dirconf_t;
static dirconf_t dirconf;

static const fmed_conf_arg dir_conf_args[] = {
	{ "expand",  FMC_BOOL8,  FMC_O(dirconf_t, expand) },
};


int dir_conf(fmed_conf_ctx *ctx)
{
	dirconf.expand = 1;
	fmed_conf_addctx(ctx, &dirconf, dir_conf_args);
	return 0;
}

static void* dir_open(fmed_filt *d)
{
	ffdirexp dr;
	const char *fn, *dirname;
	fmed_que_entry e, *first, *cur;

	if (FMED_PNULL == (dirname = d->track->getvalstr(d->trk, "input")))
		return NULL;

	if (dirconf.expand) {
		dir_open_r(dirname, d);
		return FMED_FILT_DUMMY;
	}

	if (0 != ffdir_expopen(&dr, (char*)dirname, 0)) {
		if (fferr_last() != ENOMOREFILES) {
			syserrlog(core, d->trk, "dir", "%s", ffdir_open_S);
			return NULL;
		}
		return FMED_FILT_DUMMY;
	}

	first = (void*)fmed_getval("queue_item");
	cur = first;

	while (NULL != (fn = ffdir_expread(&dr))) {
		ffmem_zero_obj(&e);
		ffstr_setz(&e.url, fn);
		void *prev = cur;
		cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER, &e, prev);
		qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, prev);
	}

	ffdir_expclose(&dr);
	qu->cmd(FMED_QUE_RM, first);
	return FMED_FILT_DUMMY;
}

/**
'include' filter matches files only.
'exclude' filter matches files & directories.
Return TRUE if filename matches user's filename wildcards. */
static ffbool file_matches(fmed_filt *d, const char *fn, ffbool dir)
{
	size_t fnlen = ffsz_len(fn);
	const ffstr *wc;
	ffbool ok = 1;

	if (!dir) {
		ok = (d->include_files.len == 0);
		FFARR_WALKT(&d->include_files, wc, ffstr) {
			if (0 == ffs_wildcard(wc->ptr, wc->len, fn, fnlen, FFS_WC_ICASE)) {
				ok = 1;
				break;
			}
		}
		if (!ok)
			return 0;
	}

	FFARR_WALKT(&d->exclude_files, wc, ffstr) {
		if (0 == ffs_wildcard(wc->ptr, wc->len, fn, fnlen, FFS_WC_ICASE)) {
			ok = 0;
			break;
		}
	}
	return ok;
}

struct dir_ent {
	char *dir;
	ffchain_item sib;
};

/*
. Scan directory
. Add files to queue;  gather all directories into chain
. Get next directory from chain and scan it;  new directories will be added after this directory

Example:
.:
 (dir1)
 (dir2)
 file
dir1:
 (dir1/dir11)
 dir1/file
 dir1/dir11/file
dir2:
 dir2/file
*/
static int dir_open_r(const char *dirname, fmed_filt *d)
{
	ffdirexp dr;
	const char *fn;
	fmed_que_entry e, *first, *prev_qent;
	fffileinfo fi;
	ffchain chain;
	ffchain_item *lprev, *lcur;
	struct dir_ent *de;
	ffchain mblocks;
	ffmblk *mblk;
	ffchain_item *it;

	ffchain_init(&mblocks);

	ffchain_init(&chain);
	lprev = ffchain_sentl(&chain);
	lcur = lprev;

	first = (void*)fmed_getval("queue_item");
	prev_qent = first;

	for (;;) {

		dbglog(core, d->trk, NULL, "scanning %s", dirname);

		if (0 != ffdir_expopen(&dr, (char*)dirname, 0)) {
			if (fferr_last() != ENOMOREFILES)
				syserrlog(core, d->trk, NULL, "%s: %s", ffdir_open_S, dirname);
			goto next;
		}

		while (NULL != (fn = ffdir_expread(&dr))) {

			if (0 != fffile_infofn(fn, &fi)) {
				syserrlog(core, d->trk, NULL, "%s: %s", fffile_info_S, fn);
				continue;
			}

			if (!file_matches(d, ffdir_expname(&dr, fn), fffile_isdir(fffile_infoattr(&fi))))
				continue;

			if (fffile_isdir(fffile_infoattr(&fi))) {

				mblk = ffmblk_chain_last(&mblocks);
				if (mblk == NULL || ffarr_unused(&mblk->buf) == 0) {

					// allocate a new block with fixed size = 4kb
					if (NULL == (mblk = ffmblk_chain_push(&mblocks))
						|| NULL == ffarr_allocT(&mblk->buf, 4096 / sizeof(struct dir_ent), struct dir_ent)) {
						syserrlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
						goto end;
					}
				}

				de = ffarr_pushT(&mblk->buf, struct dir_ent);
				if (NULL == (de->dir = ffsz_alcopyz(fn))) {
					syserrlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
					goto end;
				}
				ffchain_append(&de->sib, lprev);
				lprev = &de->sib;
				continue;
			}

			ffmem_zero_obj(&e);
			ffstr_setz(&e.url, fn);
			void *cur = (void*)qu->cmdv(FMED_QUE_ADDAFTER | FMED_QUE_MORE, &e, prev_qent);
			qu->cmdv(FMED_QUE_COPYTRACKPROPS, cur, prev_qent);
			prev_qent = cur;
		}

		ffdir_expclose(&dr);
		ffmem_zero_obj(&dr);

next:
		lcur = lcur->next;
		if (lcur == ffchain_sentl(&chain))
			break;
		de = FF_GETPTR(struct dir_ent, sib, lcur);
		dirname = de->dir;
		lprev = lcur;
	}

end:
	qu->cmd2(FMED_QUE_ADD | FMED_QUE_ADD_DONE, NULL, 0);
	ffdir_expclose(&dr);
	FFCHAIN_FOR(&mblocks, it) {
		mblk = FF_GETPTR(ffmblk, sib, it);
		it = it->next;
		FFARR_WALKT(&mblk->buf, de, struct dir_ent) {
			ffmem_safefree(de->dir);
		}
		ffmblk_free(mblk);
	}
	qu->cmd(FMED_QUE_RM, first);
	return 0;
}

static void dir_close(void *ctx)
{
}

static int dir_process(void *ctx, fmed_filt *d)
{
	return FMED_RFIN;
}
