/** Shared code for audio I/O
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>
#include <ffaudio/audio.h>


#define warnlog1(trk, ...)  fmed_warnlog(a->core, trk, NULL, __VA_ARGS__)
#define errlog1(trk, ...)  fmed_errlog(a->core, trk, NULL, __VA_ARGS__)
#define dbglog1(trk, ...)  fmed_dbglog(a->core, trk, NULL, __VA_ARGS__)

#define ABUF_CLOSE_WAIT  3000


struct fmt_pair {
	ffpcm bad, good;
};

static inline ffpcm* fmt_conv_find(ffvec *fmts, const ffpcm *fmt)
{
	struct fmt_pair *it;
	FFSLICE_WALK(fmts, it) {
		if (ffpcm_eq(fmt, &it->bad))
			return &it->good;
	}
	return NULL;
}

static inline void fmt_conv_add(ffvec *fmts, const ffpcm *bad, const ffpcmex *good)
{
	ffpcm *g = fmt_conv_find(fmts, bad);
	if (g == NULL) {
		struct fmt_pair *p = ffvec_pushT(fmts, struct fmt_pair);
		p->bad = *bad;
		g = &p->good;
	}
	ffpcm_fmtcopy(g, good);
}

static const ushort ffaudio_formats[] = {
	FFAUDIO_F_INT8, FFAUDIO_F_INT16, FFAUDIO_F_INT24, FFAUDIO_F_INT32, FFAUDIO_F_INT24_4, FFAUDIO_F_FLOAT32,
};
static const char ffaudio_formats_str[][8] = {
	"int8", "int16", "int24", "int32", "int24_4", "float32",
};
static const ushort ffpcm_formats[] = {
	FFPCM_8, FFPCM_16, FFPCM_24, FFPCM_32, FFPCM_24_4, FFPCM_FLOAT,
};

static inline int ffpcm_to_ffaudio(uint f)
{
	int i = ffarrint16_find(ffpcm_formats, FF_COUNT(ffpcm_formats), f);
	if (i < 0)
		return -1;
	return ffaudio_formats[i];
}

static inline int ffaudio_to_ffpcm(uint f)
{
	int i = ffarrint16_find(ffaudio_formats, FF_COUNT(ffaudio_formats), f);
	if (i < 0)
		return -1;
	return ffpcm_formats[i];
}

static inline const char* ffaudio_format_str(uint f)
{
	int i = ffarrint16_find(ffaudio_formats, FF_COUNT(ffaudio_formats), f);
	return ffaudio_formats_str[i];
}

static inline const char* ffpcm_format_str(uint f)
{
	int i = ffarrint16_find(ffpcm_formats, FF_COUNT(ffpcm_formats), f);
	return ffaudio_formats_str[i];
}


static int audio_dev_list(const fmed_core *core, const ffaudio_interface *audio, fmed_adev_ent **ents, uint flags, const char *mod_name)
{
	ffarr a = {};
	ffaudio_dev *d;
	fmed_adev_ent *e;
	int r, rr = -1;

	uint f;
	if (flags == FMED_ADEV_PLAYBACK)
		f = FFAUDIO_DEV_PLAYBACK;
	else if (flags == FMED_ADEV_CAPTURE)
		f = FFAUDIO_DEV_CAPTURE;
	else
		return -1;
	d = audio->dev_alloc(f);

	for (;;) {
		r = audio->dev_next(d);
		if (r == 1)
			break;
		else if (r < 0) {
			fmed_errlog(core, NULL, mod_name, "dev_next(): %s", audio->dev_error(d));
			goto end;
		}

		if (NULL == (e = ffarr_pushgrowT(&a, 4, fmed_adev_ent)))
			goto end;
		ffmem_zero_obj(e);

		if (NULL == (e->name = ffsz_dup(audio->dev_info(d, FFAUDIO_DEV_NAME))))
			goto end;

		e->default_device = !!audio->dev_info(d, FFAUDIO_DEV_IS_DEFAULT);

		const ffuint *def_fmt = (void*)audio->dev_info(d, FFAUDIO_DEV_MIX_FORMAT);
		if (def_fmt != NULL) {
			e->default_format.format = ffaudio_to_ffpcm(def_fmt[0]);
			e->default_format.sample_rate = def_fmt[1];
			e->default_format.channels = def_fmt[2];
		}
	}

	if (NULL == (e = ffarr_pushT(&a, fmed_adev_ent)))
		goto end;
	e->name = NULL;
	*ents = (void*)a.ptr;
	rr = a.len - 1;

end:
	audio->dev_free(d);
	if (rr < 0) {
		FFARR_WALKT(&a, e, fmed_adev_ent) {
			ffmem_free(e->name);
		}
		ffarr_free(&a);
	}
	return rr;
}

static void audio_dev_listfree(fmed_adev_ent *ents)
{
	fmed_adev_ent *e;
	for (e = ents;  e->name != NULL;  e++) {
		ffmem_free(e->name);
	}
	ffmem_free(ents);
}

typedef struct audio_out audio_out;
void audio_dev_clear(audio_out *a);
int audio_dev_cmd(int cmd, void *adev_ctx)
{
	switch (cmd) {
	case FMED_ADEV_CMD_CLEAR:
		audio_dev_clear(adev_ctx);
		break;
	}
	return 0;
}

/** Get device by index */
static int audio_devbyidx(const ffaudio_interface *audio, ffaudio_dev **d, uint idev, uint flags)
{
	*d = audio->dev_alloc(flags);

	for (uint i = 0;  ;  i++) {

		int r = audio->dev_next(*d);
		if (r != 0) {
			audio->dev_free(*d);
			*d = NULL;
			return r;
		}

		if (i + 1 == idev)
			break;
	}

	return 0;
}

#include <adev/audio-capture.h>


struct audio_out {
	// input
	const fmed_core *core;
	const ffaudio_interface *audio;
	uint buffer_length_msec; // input, output
	uint try_open;
	uint dev_idx; // 0:default
	const fmed_track *track;
	fmed_track_obj *trk;
	fmed_filt *fx;
	uint aflags;
	int err_code; // enum FFAUDIO_E
	int handle_dev_offline;

	// runtime
	ffaudio_buf *stream;
	ffaudio_dev *dev;
	uint async;
	uint clear :1;

	// user's
	uint state;
	uint reconnect :1;
};

/**
Return FFAUDIO_E* */
static inline int audio_out_open(audio_out *a, fmed_filt *d, const ffpcm *fmt)
{
	if (!ffsz_eq(d->datatype, "pcm")) {
		errlog1(d->trk, "unsupported input data type: %s", d->datatype);
		return FFAUDIO_ERROR;
	}

	int rc = FFAUDIO_ERROR, r;
	ffaudio_conf conf = {};

	if (a->dev == NULL
		&& a->dev_idx != 0) {
		if (0 != audio_devbyidx(a->audio, &a->dev, a->dev_idx, FFAUDIO_DEV_PLAYBACK)) {
			errlog1(d->trk, "no audio device by index #%u", a->dev_idx);
			goto end;
		}
	}

	if (a->dev != NULL)
		conf.device_id = a->audio->dev_info(a->dev, FFAUDIO_DEV_ID);

	if (NULL == (a->stream = a->audio->alloc())) {
		errlog1(d->trk, "audio buffer alloc");
		goto end;
	}

	int afmt = ffpcm_to_ffaudio(fmt->format);
	if (afmt < 0) {
		errlog1(d->trk, "format not supported", 0);
		goto end;
	}
	conf.format = afmt;
	conf.sample_rate = fmt->sample_rate;
	conf.channels = fmt->channels;

	conf.buffer_length_msec = a->buffer_length_msec;

	uint aflags = a->aflags;
	ffaudio_conf in_conf = conf;
	dbglog1(d->trk, "opening device #%d, %s/%u/%u, flags:%xu"
		, a->dev_idx
		, ffaudio_format_str(conf.format), conf.sample_rate, conf.channels
		, aflags);
	r = a->audio->open(a->stream, &conf, FFAUDIO_PLAYBACK | FFAUDIO_O_NONBLOCK | aflags);

	if (r == FFAUDIO_EFORMAT) {
		if (a->try_open) {
			int new_format = 0;
			if (conf.format != in_conf.format) {
				d->audio.convfmt.format = ffaudio_to_ffpcm(conf.format);
				new_format = 1;
			}

			if (conf.sample_rate != in_conf.sample_rate) {
				d->audio.convfmt.sample_rate = conf.sample_rate;
				new_format = 1;
			}

			if (conf.channels != in_conf.channels) {
				d->audio.convfmt.channels = conf.channels;
				new_format = 1;
			}

			if (new_format) {
				rc = FFAUDIO_EFORMAT;
				goto end;
			}
		}

		errlog1(d->trk, "open(): unsupported format", 0);
		goto end;

	} else if (r == FFAUDIO_ECONNECTION) {
		warnlog1(d->trk, "lost connection to audio server: %s", a->audio->error(a->stream));
		rc = FFAUDIO_ECONNECTION;
		goto end;

	} else if (r != 0) {
		errlog1(d->trk, "open() device #%u: %s  format:%s/%u/%u"
			, a->dev_idx
			, a->audio->error(a->stream)
			, ffaudio_format_str(conf.format), conf.sample_rate, conf.channels);
		rc = FFAUDIO_ERROR;
		goto end;
	}

	a->buffer_length_msec = conf.buffer_length_msec;
	rc = 0;

end:
	if (rc != 0) {
		a->audio->free(a->stream);
		a->stream = NULL;
	}
	if (rc == FFAUDIO_ERROR)
		a->track->cmd(a->trk, FMED_TRACK_STOPPED);
	return rc;
}

static inline void audio_out_onplay(void *param)
{
	audio_out *a = param;
	if (!a->async)
		return;
	a->async = 0;
	a->track->cmd(a->trk, FMED_TRACK_WAKE);
}

void audio_dev_clear(audio_out *a)
{
	dbglog1(a->trk, "stop");
	if (0 != a->audio->stop(a->stream))
		warnlog1(a->trk, "audio.stop: %s", a->audio->error(a->stream));
	a->clear = 1;
	audio_out_onplay(a);
}

static inline int audio_out_write(audio_out *a, fmed_filt *d)
{
	int r;

	if (a->clear) {
		a->clear = 0;
		dbglog1(d->trk, "stop/clear");
		if (0 != a->audio->stop(a->stream))
			warnlog1(a->trk, "audio.stop: %s", a->audio->error(a->stream));
		if (0 != a->audio->clear(a->stream))
			warnlog1(d->trk, "audio.clear: %s", a->audio->error(a->stream));
		if (d->seek_req)
			return FMED_RMORE;
	}

	if (d->snd_output_pause) {
		d->snd_output_pause = 0;
		d->track->cmd(d->trk, FMED_TRACK_PAUSE);
		if (0 != a->audio->stop(a->stream))
			warnlog1(d->trk, "pause: audio.stop: %s", a->audio->error(a->stream));
		return FMED_RASYNC;
	}

	while (d->datalen != 0) {

		r = a->audio->write(a->stream, d->data, d->datalen);
		if (r > 0) {
			//
		} else if (r == 0) {
			a->async = 1;
			return FMED_RASYNC;

		} else if (r == -FFAUDIO_ESYNC) {
			warnlog1(d->trk, "underrun detected", 0);
			continue;

		} else if (r == -FFAUDIO_EDEV_OFFLINE && a->handle_dev_offline) {
			warnlog1(d->trk, "audio device write: device disconnected: %s", a->audio->error(a->stream));
			a->err_code = FFAUDIO_EDEV_OFFLINE;
			return FMED_RERR;

		} else {
			ffstr extra = {};
			if (r == -FFAUDIO_EDEV_OFFLINE)
				ffstr_setz(&extra, "device disconnected: ");
			errlog1(d->trk, "audio device write: %S%s", &extra, a->audio->error(a->stream));
			a->err_code = -r;
			a->track->cmd(a->trk, FMED_TRACK_STOPPED);
			return FMED_RERR;
		}

		d->data += r;
		d->datalen -= r;
		dbglog1(d->trk, "written %u bytes"
			, r);
	}

	if (d->flags & FMED_FLAST) {

		r = a->audio->drain(a->stream);
		if (r == 1)
			return FMED_RDONE;
		else if (r < 0) {
			errlog1(d->trk, "drain(): %s", a->audio->error(a->stream));
			return FMED_RERR;
		}

		a->async = 1;
		return FMED_RASYNC; //wait until all filled bytes are played
	}

	return FMED_RMORE;
}
