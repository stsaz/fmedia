/** AVI input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/mformat/avi.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/mmtag.h>
#include <FF/array.h>
#include <FFOS/error.h>


static const fmed_core *core;

typedef struct fmed_avi {
	ffavi avi;
	uint state;
} fmed_avi;


//FMEDIA MODULE
static const void* avi_iface(const char *name);
static int avi_sig(uint signo);
static void avi_destroy(void);
static const fmed_mod fmed_avi_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&avi_iface, &avi_sig, &avi_destroy
};

//INPUT
static void* avi_open(fmed_filt *d);
static void avi_close(void *ctx);
static int avi_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_avi_input = {
	&avi_open, &avi_process, &avi_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_avi_mod;
}


static const void* avi_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_avi_input;
	return NULL;
}

static int avi_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;
	}
	return 0;
}

static void avi_destroy(void)
{
}


static void* avi_open(fmed_filt *d)
{
	fmed_avi *a = ffmem_tcalloc1(fmed_avi);
	if (a == NULL) {
		errlog(core, d->trk, NULL, "%s", ffmem_alloc_S);
		return NULL;
	}
	ffavi_init(&a->avi);
	a->avi.options = FFAVI_O_TAGS;
	return a;
}

static void avi_close(void *ctx)
{
	fmed_avi *a = ctx;
	ffavi_close(&a->avi);
	ffmem_free(a);
}

static void avi_meta(fmed_avi *a, fmed_filt *d)
{
	ffstr name, val;
	if (a->avi.tag == -1)
		return;
	ffstr_setz(&name, ffmmtag_str[a->avi.tag]);
	ffstr_set2(&val, &a->avi.tagval);
	d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
}

static const ushort avi_codecs[] = {
	FFAVI_AUDIO_AAC, FFAVI_AUDIO_MP3,
};
static const char* const avi_codecs_str[] = {
	"aac.decode", "mpeg.decode",
};

static int avi_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_avi *a = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffstr_set(&a->avi.data, d->data, d->datalen);
		d->datalen = 0;
	}

	switch (a->state) {
	case I_HDR:
		break;

	case I_DATA:
		break;
	}

	if (d->flags & FMED_FLAST)
		a->avi.fin = 1;

	for (;;) {
		r = ffavi_read(&a->avi);
		switch (r) {
		case FFAVI_RMORE:
			if (d->flags & FMED_FLAST) {
				errlog(core, d->trk, NULL, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFAVI_RDONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFAVI_RDATA:
			goto data;

		case FFAVI_RHDR: {
			int i = ffint_find2(avi_codecs, FFCNT(avi_codecs), a->avi.info.format);
			if (i == -1) {
				errlog(core, d->trk, NULL, "unsupported codec: %xu", a->avi.info.format);
				return FMED_RERR;
			}

			const char *codec = avi_codecs_str[i];
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = a->avi.info.channels;
			d->audio.fmt.sample_rate = a->avi.info.sample_rate;
			d->audio.total = a->avi.info.total_samples;
			d->audio.bitrate = a->avi.info.bitrate;

			d->out = a->avi.info.asc.ptr,  d->outlen = a->avi.info.asc.len;
			a->state = I_DATA;
			return FMED_RDATA;
		}

		case FFAVI_RTAG:
			avi_meta(a, d);
			break;

		case FFAVI_RSEEK:
			// d->input.seek = ffavi_seekoff(&a->avi);
			return FMED_RMORE;

		case FFAVI_RWARN:
			warnlog(core, d->trk, NULL, "ffavi_read(): %s", ffavi_errstr(&a->avi));
			break;

		case FFAVI_RERR:
		default:
			errlog(core, d->trk, NULL, "ffavi_read(): %s", ffavi_errstr(&a->avi));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = ffavi_cursample(&a->avi);
	d->out = a->avi.out.ptr,  d->outlen = a->avi.out.len;
	return FMED_RDATA;
}
