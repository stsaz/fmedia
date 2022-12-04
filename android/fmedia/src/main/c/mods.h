/** fmedia/Android: core
2022, Simon Zolin */

struct filter_pair {
	const char *ext;
	const char *name;
};

static int ext_filter_keyeq(void *opaque, const void *key, ffsize keylen, void *val)
{
	const struct filter_pair *fp = val;
	ffstr s = FFSTR_INITN(key, keylen);
	return ffstr_ieqz(&s, fp->ext);
}

void mods_init()
{
	ffmap_init(&cx->ext_filter, ext_filter_keyeq);
	static const struct filter_pair filters[] = {
		{ "flac",	"fmt.flac" },
		{ "m4a",	"fmt.mp4" },
		{ "mp3",	"fmt.mp3" },
		{ "mp4",	"fmt.mp4" },
		{ "ogg",	"fmt.ogg" },
		{ "opus",	"fmt.ogg" },
	};
	for (uint i = 0;  i != FF_COUNT(filters);  i++) {
		ffmap_add(&cx->ext_filter, filters[i].ext, ffsz_len(filters[i].ext), (void*)&filters[i]);
	}
}

const char* mods_filter_byext(ffstr ext)
{
	const struct filter_pair *f = ffmap_find(&cx->ext_filter, ext.ptr, ext.len, NULL);
	if (f == NULL)
		return NULL;
	return f->name;
}

extern const fmed_track track_iface;

const void* mods_find(const char *name)
{
	static const char *const names[] = {
		"core.track",
	};
	static const void *const mods[] = {
		/*"core.track"*/	&track_iface,
	};
	int i = ffszarr_findsorted(names, FF_COUNT(names), name, ffsz_len(name));
	if (i < 0) {
		errlog0("%s: no such module");
		return NULL;
	}
	return mods[i];
}

extern const fmed_filter _fmed_format_detector;
extern const fmed_filter ctl_filter;
extern const fmed_filter file_input;
extern const fmed_filter file_output;
extern const fmed_filter flac_input;
extern const fmed_filter mp3_input;
extern const fmed_filter mp3_copy;
extern const fmed_filter mp4_input;
extern const fmed_filter ogg_input;
extern const fmed_filter opusmeta_input;
extern const fmed_filter vorbismeta_input;

extern int ogg_in_conf(fmed_conf_ctx *ctx);

const fmed_filter* mods_filter_byname(const char *name)
{
	static const char *const names[] = {
		"core.file",
		"core.filew",
		"ctl",
		"fmt.detector",
		"fmt.flac",
		"fmt.mp3",
		"fmt.mp3-copy",
		"fmt.mp4",
		"fmt.ogg",
		"fmt.opusmeta",
		"fmt.vorbismeta",
	};
	static const fmed_filter *const filters[] = {
		/*"core.file"*/	&file_input,
		/*"core.filew"*/	&file_output,
		/*"ctl"*/	&ctl_filter,
		/*"fmt.detector"*/	&_fmed_format_detector,
		/*"fmt.flac"*/	&flac_input,
		/*"fmt.mp3"*/	&mp3_input,
		/*"fmt.mp3-copy"*/	&mp3_copy,
		/*"fmt.mp4"*/	&mp4_input,
		/*"fmt.ogg"*/	&ogg_input,
		/*"fmt.opusmeta"*/	&opusmeta_input,
		/*"fmt.vorbismeta"*/	&vorbismeta_input,
	};
	int i = ffszarr_findsorted(names, FF_COUNT(names), name, ffsz_len(name));
	if (i < 0) {
		errlog0("%s: no such filter", name);
		return NULL;
	}

	if (filters[i] == &ogg_input) {
		fmed_conf_ctx cc = {};
		ogg_in_conf(&cc);
	}

	return filters[i];
}
