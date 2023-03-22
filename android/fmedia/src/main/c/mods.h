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

/** Get filter by input file extension */
const char* mods_ifilter_byext(ffstr ext)
{
	const struct filter_pair *f = ffmap_find(&cx->ext_filter, ext.ptr, ext.len, NULL);
	if (f == NULL)
		return NULL;
	return f->name;
}

/** Get filter by output file extension */
const char* mods_ofilter_byext(ffstr ext)
{
	static const char *const exts[] = {
		"flac",
		"m4a",
		"mp3",
	};
	static const char *const fnames[] = {
		/*"flac"*/ "fmt.flacw",
		/*"m4a"*/ "fmt.mp4w",
		/*"mp3"*/ "fmt.mp3w",
	};
	int i = ffszarr_findsorted(exts, FF_COUNT(exts), ext.ptr, ext.len);
	if (i < 0) {
		errlog0("'%S': no filter for output extension", &ext);
		return NULL;
	}
	return fnames[i];
}

extern const fmed_track track_iface;
extern const fmed_queue fmed_que_mgr;

const void* mods_find(const char *name)
{
	static const char *const names[] = {
		"core.queue",
		"core.track",
	};
	static const void *const mods[] = {
		/*"core.queue"*/	&fmed_que_mgr,
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
extern const fmed_filter flac_output;
extern const fmed_filter mp3_input;
extern const fmed_filter mp3_copy;
extern const fmed_filter mp4_input;
extern const fmed_filter mp4_output;
extern const fmed_filter ogg_input;
extern const fmed_filter opusmeta_input;
extern const fmed_filter vorbismeta_input;
extern const fmed_filter fmed_m3u_input;

extern const fmed_filter fmed_sndmod_autoconv;
extern const fmed_filter fmed_sndmod_conv;
extern const fmed_filter fmed_sndmod_until;
extern const fmed_filter fmed_sndmod_gain;
extern const struct fmed_filter2 fmed_soxr;

extern const fmed_filter aac_input;
extern const fmed_filter aac_output;
extern const fmed_filter mpeg_decode_filt;
extern const fmed_filter mod_flac_enc;
extern const fmed_filter fmed_flac_dec;
extern const fmed_filter alac_input;

extern int ogg_in_conf(fmed_conf_ctx *ctx);
extern int aac_out_config(fmed_conf_ctx *ctx);
extern int flac_enc_config(fmed_conf_ctx *ctx);
extern int flac_out_config(fmed_conf_ctx *ctx);

static const fmed_mod* mod_load(const char *name_so)
{
	ffdl dl = ffdl_open(name_so, 0);
	if (dl == FFDL_NULL) {
		errlog0("%s: can't load: %s", name_so, ffdl_errstr());
		return NULL;
	}
	fmed_getmod_t getmod = ffdl_addr(dl, "fmed_getmod");
	if (getmod == NULL) {
		ffdl_close(dl);
		errlog0("%s: can't load: %s", name_so, ffdl_errstr());
		return NULL;
	}
	const fmed_mod *m = getmod(core);
	if (m == NULL) {
		ffdl_close(dl);
		errlog0("%s: can't load: fmed_getmod()", name_so);
		return NULL;
	}
	// ffdl_close(dl);
	return m;
}

const fmed_filter* mods_filter_byname(const char *name)
{
	if (ffsz_eq(name, "aaudio.in")) {
		const fmed_mod *m = mod_load("aaudio.so");
		if (m == NULL)
			return NULL;
		const fmed_filter *f = m->iface("in");
		if (f == NULL) {
			errlog0("%s: no such filter", name);
			return NULL;
		}
		return f;
	}

	static const char *const names[] = {
		"aac.decode",
		"aac.encode",
		"afilter.autoconv",
		"afilter.conv",
		"afilter.gain",
		"afilter.until",
		"alac.decode",
		"core.file",
		"core.filew",
		"ctl",
		"flac.decode",
		"flac.encode",
		"fmt.detector",
		"fmt.flac",
		"fmt.flacw",
		"fmt.m3u",
		"fmt.mp3",
		"fmt.mp3-copy",
		"fmt.mp4",
		"fmt.mp4w",
		"fmt.ogg",
		"fmt.opusmeta",
		"fmt.vorbismeta",
		"mpeg.decode",
		"soxr.conv",
	};
	static const fmed_filter *const filters[] = {
		/*"aac.decode"*/	&aac_input,
		/*"aac.encode"*/	&aac_output,
		/*"afilter.autoconv"*/	&fmed_sndmod_autoconv,
		/*"afilter.conv"*/	&fmed_sndmod_conv,
		/*"afilter.gain"*/	&fmed_sndmod_gain,
		/*"afilter.until"*/	&fmed_sndmod_until,
		/*"alac.decode"*/	&alac_input,
		/*"core.file"*/	&file_input,
		/*"core.filew"*/	&file_output,
		/*"ctl"*/	&ctl_filter,
		/*"flac.decode"*/	&fmed_flac_dec,
		/*"flac.encode"*/	&mod_flac_enc,
		/*"fmt.detector"*/	&_fmed_format_detector,
		/*"fmt.flac"*/	&flac_input,
		/*"fmt.flacw"*/	&flac_output,
		/*"fmt.m3u"*/	&fmed_m3u_input,
		/*"fmt.mp3"*/	&mp3_input,
		/*"fmt.mp3-copy"*/	&mp3_copy,
		/*"fmt.mp4"*/	&mp4_input,
		/*"fmt.mp4w"*/	&mp4_output,
		/*"fmt.ogg"*/	&ogg_input,
		/*"fmt.opusmeta"*/	&opusmeta_input,
		/*"fmt.vorbismeta"*/	&vorbismeta_input,
		/*"mpeg.decode"*/	&mpeg_decode_filt,
		/*"soxr.conv"*/	(fmed_filter*)&fmed_soxr,
	};
	int i = ffszarr_findsorted(names, FF_COUNT(names), name, ffsz_len(name));
	if (i < 0) {
		errlog0("%s: no such filter", name);
		return NULL;
	}

	if (filters[i] == &ogg_input) {
		fmed_conf_ctx cc = {};
		ogg_in_conf(&cc);

	} else if (filters[i] == &aac_output) {
		fmed_conf_ctx cc = {};
		aac_out_config(&cc);

	} else if (filters[i] == &mod_flac_enc) {
		fmed_conf_ctx cc = {};
		flac_enc_config(&cc);

	} else if (filters[i] == &flac_output) {
		fmed_conf_ctx cc = {};
		flac_out_config(&cc);
	}

	return filters[i];
}
