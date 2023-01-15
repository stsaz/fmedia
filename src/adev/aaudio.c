/** fmedia: AAudio input
2023, Simon Zolin */

#include <fmedia.h>
#include <adev/audio.h>

static const fmed_core *core;

struct aai {
	audio_in in;
};

static void aai_close(void *ctx)
{
	struct aai *c = ctx;
	audio_in_close(&c->in);
	ffmem_free(c);
}

static void* aai_open(fmed_track_info *d)
{
	struct aai *c = ffmem_new(struct aai);
	audio_in *a = &c->in;
	a->core = core;
	a->audio = &ffaaudio;
	a->track = d->track;
	a->trk = d->trk;
	a->aflags |= FFAUDIO_O_UNSYNC_NOTIFY;
	a->aflags |= (d->ai_power_save) ? FFAUDIO_O_POWER_SAVE : 0;
	a->aflags |= (d->ai_exclusive) ? FFAUDIO_O_EXCLUSIVE : 0;
	a->recv_events = 1;
	a->buffer_length_msec = d->a_in_buf_time;

	if (0 != audio_in_open(a, d))
		goto err;

	return c;

err:
	aai_close(c);
	return NULL;
}

static int aai_read(void *ctx, fmed_track_info *d)
{
	struct aai *c = ctx;
	return audio_in_read(&c->in, d);
}

static const fmed_filter aai_f = { aai_open, aai_read, aai_close };


static const void* aa_iface(const char *name)
{
	if (ffsz_eq(name, "in"))
		return &aai_f;
	return NULL;
}

static int aa_conf(const char *name, fmed_conf_ctx *ctx)
{
	return -1;
}

static int aa_sig(uint signo)
{
	return 0;
}

static void aa_destroy(void)
{
}

static const fmed_mod fmed_aa = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	.iface = aa_iface,
	.sig = aa_sig,
	.destroy = aa_destroy,
	.conf = aa_conf,
};

FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_aa;
}
