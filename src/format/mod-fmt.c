/** fmedia: fmt module
2021, Simon Zolin */

#include <fmedia.h>

const fmed_core *core;

extern const fmed_filter avi_input;
extern const fmed_filter caf_input;
extern const fmed_filter mkv_input;
extern const fmed_filter mp4_input;
extern const fmed_filter mp4_output;
extern const fmed_filter ogg_input;
extern const fmed_filter ogg_output;
extern const fmed_filter raw_input;
extern const fmed_filter wav_input;
extern const fmed_filter wav_output;
const void* mod_iface(const char *name)
{
	if (ffsz_eq(name, "avi"))
		return &avi_input;
	else if (ffsz_eq(name, "caf"))
		return &caf_input;
	else if (ffsz_eq(name, "mkv"))
		return &mkv_input;
	else if (ffsz_eq(name, "mp4"))
		return &mp4_input;
	else if (ffsz_eq(name, "mp4-write"))
		return &mp4_output;
	else if (ffsz_eq(name, "ogg"))
		return &ogg_input;
	else if (ffsz_eq(name, "ogg-write"))
		return &ogg_output;
	else if (ffsz_eq(name, "raw"))
		return &raw_input;
	else if (ffsz_eq(name, "wav"))
		return &wav_input;
	else if (ffsz_eq(name, "wav-write"))
		return &wav_output;
	return NULL;
}

int mod_sig(uint signo)
{
	return 0;
}

void mod_destroy()
{
}

extern int ogg_in_conf(ffpars_ctx *ctx);
extern int ogg_out_conf(ffpars_ctx *ctx);
int mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (ffsz_eq(name, "ogg"))
		return ogg_in_conf(ctx);
	else if (ffsz_eq(name, "ogg-write"))
		return ogg_out_conf(ctx);
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
