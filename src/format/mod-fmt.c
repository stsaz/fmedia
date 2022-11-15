/** fmedia: fmt module
2021, Simon Zolin */

#include <fmedia.h>

#define dbglog1(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)

const fmed_core *core;

extern const fmed_filter aac_adts_input;
extern const fmed_filter aac_adts_output;
extern const fmed_filter ape_input;
extern const fmed_filter avi_input;
extern const fmed_filter caf_input;
extern const fmed_filter mkv_input;
extern const fmed_filter mp3_copy;
extern const fmed_filter mp3_input;
extern const fmed_filter mp3_output;
extern const fmed_filter mp4_input;
extern const fmed_filter mp4_output;
extern const fmed_filter mpc_input;
extern const fmed_filter ogg_input;
extern const fmed_filter ogg_output;
extern const fmed_filter opusmeta_input;
extern const fmed_filter raw_input;
extern const fmed_filter vorbismeta_input;
extern const fmed_filter wav_input;
extern const fmed_filter wav_output;
extern const fmed_filter wv_input;
extern const fmed_edittags edittags_filt;
const void* mod_iface(const char *name)
{
	static const char *const names[] = {
		"aac",
		"aac-write",
		"ape",
		"avi",
		"caf",
		"edit-tags",
		"mkv",
		"mp3",
		"mp3-copy",
		"mp3-write",
		"mp4",
		"mp4-write",
		"mpc",
		"ogg",
		"ogg-write",
		"opusmeta",
		"raw",
		"vorbismeta",
		"wav",
		"wav-write",
		"wv",
	};
	static const fmed_filter *const ifaces[] = {
		/*"aac"*/	&aac_adts_input,
		/*"aac-write"*/	&aac_adts_output,
		/*"ape"*/	&ape_input,
		/*"avi"*/	&avi_input,
		/*"caf"*/	&caf_input,
		/*"edit-tags"*/	(fmed_filter*)&edittags_filt,
		/*"mkv"*/	&mkv_input,
		/*"mp3"*/	&mp3_input,
		/*"mp3-copy"*/	&mp3_copy,
		/*"mp3-write"*/	&mp3_output,
		/*"mp4"*/	&mp4_input,
		/*"mp4-write"*/	&mp4_output,
		/*"mpc"*/	&mpc_input,
		/*"ogg"*/	&ogg_input,
		/*"ogg-write"*/	&ogg_output,
		/*"opusmeta"*/	&opusmeta_input,
		/*"raw"*/	&raw_input,
		/*"vorbismeta"*/	&vorbismeta_input,
		/*"wav"*/	&wav_input,
		/*"wav-write"*/	&wav_output,
		/*"wv"*/	&wv_input,
	};

	int i = ffszarr_findsorted(names, FF_COUNT(names), name, ffsz_len(name));
	if (i < 0)
		return NULL;
	return ifaces[i];
}

int mod_sig(uint signo)
{
	return 0;
}

void mod_destroy()
{
}

extern int ogg_in_conf(fmed_conf_ctx *ctx);
extern int ogg_out_conf(fmed_conf_ctx *ctx);
extern int mpeg_out_config(fmed_conf_ctx *ctx);
int mod_conf(const char *name, fmed_conf_ctx *ctx)
{
	if (ffsz_eq(name, "ogg"))
		return ogg_in_conf(ctx);
	else if (ffsz_eq(name, "ogg-write"))
		return ogg_out_conf(ctx);
	else if (ffsz_eq(name, "mp3-write"))
		return mpeg_out_config(ctx);
	return -1;
}

const fmed_mod mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	mod_iface, mod_sig, mod_destroy, mod_conf
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &mod;
}
