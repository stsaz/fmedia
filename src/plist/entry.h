/** fmedia: playlist entry
2023, Simon Zolin */

#include <util/path.h>
#include <ffbase/vector.h>

typedef struct pls_entry {
	ffvec url;
	ffvec artist;
	ffvec title;
	int duration;
	uint clear :1;
} pls_entry;

static inline void pls_entry_free(pls_entry *ent)
{
	ffvec_free(&ent->url);
	ffvec_free(&ent->artist);
	ffvec_free(&ent->title);
	ent->duration = -1;
}

/** Parse URI scheme.
Return scheme length on success. */
static inline uint ffuri_scheme(ffstr name)
{
	ffstr scheme;
	if (ffstr_matchfmt(&name, "%S://", &scheme) <= 0)
		return 0;
	return scheme.len;
}

/** Get absolute filename. */
static inline int plist_fullname(fmed_track_info *ti, const ffstr *name, ffstr *dst)
{
	const char *fn;
	ffstr path = {0};
	ffstr3 s = {0};

	if (!ffpath_abs(name->ptr, name->len)
		&& 0 == ffuri_scheme(*name)) {

		fn = ti->in_filename;
		if (ti->track->getvalstr != NULL)
			fn = ti->track->getvalstr(ti->trk, "input");
		if (NULL != ffpath_split2(fn, ffsz_len(fn), &path, NULL))
			path.len++;
	}

	if (0 == ffstr_catfmt(&s, "%S%S", &path, name))
		return 1;
	ffstr_acqstr3(dst, &s);
	return 0;
}
