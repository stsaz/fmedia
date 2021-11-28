/** fmedia: audio capture interface
2020, Simon Zolin */

typedef struct audio_in {
	// input
	const fmed_core *core;
	const ffaudio_interface *audio;
	uint dev_idx; // 0:default
	void *trk;
	const fmed_track *track;
	ffuint buffer_length_msec; // input, output
	uint loopback;
	uint aflags;

	// runtime
	ffaudio_buf *stream;
	uint64 total_samples;
	uint frame_size;
	uint async;
} audio_in;

static int audio_in_open(audio_in *a, fmed_filt *d)
{
	int r;
	ffbool first_try = 1;
	ffaudio_dev *dev = NULL;
	ffaudio_conf conf = {};

	if (a->dev_idx != 0) {
		ffuint mode = (a->loopback) ? FFAUDIO_DEV_PLAYBACK : FFAUDIO_DEV_CAPTURE;
		if (0 != audio_devbyidx(a->audio, &dev, a->dev_idx, mode)) {
			errlog1(d->trk, "no audio device by index #%u", a->dev_idx);
			goto err;
		}
		conf.device_id = a->audio->dev_info(dev, FFAUDIO_DEV_ID);
	}

	int afmt = ffpcm_to_ffaudio(d->audio.fmt.format);
	if (afmt < 0) {
		errlog1(d->trk, "format not supported", 0);
		goto err;
	}
	conf.format = afmt;
	conf.sample_rate = d->audio.fmt.sample_rate;
	conf.channels = d->audio.fmt.channels;

	conf.buffer_length_msec = (d->a_in_buf_time != 0) ? d->a_in_buf_time : a->buffer_length_msec;

	ffaudio_conf in_conf = conf;

	int aflags = (a->loopback) ? FFAUDIO_LOOPBACK : FFAUDIO_CAPTURE;
	aflags |= a->aflags;

	if (NULL == (a->stream = a->audio->alloc()))
		goto err;

	for (;;) {
		dbglog1(d->trk, "opening device #%d, %s/%u/%u"
			, a->dev_idx
			, ffaudio_format_str(conf.format), conf.sample_rate, conf.channels);
		r = a->audio->open(a->stream, &conf, aflags | FFAUDIO_O_NONBLOCK | FFAUDIO_O_UNSYNC_NOTIFY);

		if (r == FFAUDIO_EFORMAT) {
			if (first_try) {
				first_try = 0;
				int new_format = 0;

				if (conf.format != in_conf.format) {
					if (d->audio.convfmt.format == 0)
						d->audio.convfmt.format = d->audio.fmt.format;
					d->audio.fmt.format = ffaudio_to_ffpcm(conf.format);
					new_format = 1;
				}

				if (conf.sample_rate != in_conf.sample_rate) {
					if (d->audio.convfmt.sample_rate == 0)
						d->audio.convfmt.sample_rate = d->audio.fmt.sample_rate;
					d->audio.fmt.sample_rate = conf.sample_rate;
					new_format = 1;
				}

				if (conf.channels != in_conf.channels) {
					if (d->audio.convfmt.channels == 0)
						d->audio.convfmt.channels = d->audio.fmt.channels;
					d->audio.fmt.channels = conf.channels;
					new_format = 1;
				}

				if (new_format)
					continue;
			}

			if (aflags & FFAUDIO_O_HWDEV) {
				aflags &= ~FFAUDIO_O_HWDEV;
				continue;
			}

			errlog1(d->trk, "open device #%u: unsupported format: %s/%u/%u"
				, a->dev_idx
				, ffaudio_format_str(in_conf.format), in_conf.sample_rate, in_conf.channels);
			goto err;

		} else if (r != 0) {
			errlog1(d->trk, "open device #%u: %s  format:%s/%u/%u"
				, a->dev_idx
				, a->audio->error(a->stream)
				, ffaudio_format_str(in_conf.format), in_conf.sample_rate, in_conf.channels);
			goto err;
		}

		break;
	}

	dbglog1(d->trk, "opened audio capture buffer: %ums"
		, conf.buffer_length_msec);

	a->buffer_length_msec = conf.buffer_length_msec;
	a->audio->dev_free(dev);
	d->audio.fmt.ileaved = 1;
	d->datatype = "pcm";
	a->frame_size = ffpcm_size1(&d->audio.fmt);
	return 0;

err:
	a->audio->dev_free(dev);
	a->audio->free(a->stream);
	a->stream = NULL;
	return -1;
}

static void audio_in_close(audio_in *a)
{
	a->audio->free(a->stream);
}

static void audio_oncapt(void *udata)
{
	audio_in *a = udata;
	if (!a->async)
		return;
	a->async = 0;
	a->track->cmd(a->trk, FMED_TRACK_WAKE);
}

static int audio_in_read(audio_in *a, fmed_filt *d)
{
	int r;
	const void *buf;

	if (d->flags & FMED_FSTOP) {
		a->audio->stop(a->stream);
		d->outlen = 0;
		return FMED_RDONE;
	}

	for (;;) {
		r = a->audio->read(a->stream, &buf);
		if (r == -FFAUDIO_ESYNC) {
			warnlog1(d->trk, "overrun detected", 0);
			continue;

		} else if (r < 0) {
			ffstr extra = {};
			if (r == -FFAUDIO_EDEV_OFFLINE)
				ffstr_setz(&extra, "device disconnected: ");
			errlog1(d->trk, "audio device read: %S%s", &extra, a->audio->error(a->stream));
			d->outlen = 0;
			return FMED_RDONE_ERR;

		} else if (r == 0) {
			a->async = 1;
			return FMED_RASYNC;
		}
		break;
	}

	dbglog1(d->trk, "read %L bytes", r);

	d->audio.pos = a->total_samples;
	a->total_samples += r / a->frame_size;
	d->out = buf,  d->outlen = r;
	return FMED_RDATA;
}
