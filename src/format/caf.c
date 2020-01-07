/** CAF input.
Copyright (c) 2020 Simon Zolin */

#include <fmedia.h>

#include <FF/aformat/caf.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>
#include <FFOS/error.h>


#undef dbglog
#undef errlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, NULL, __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, NULL, __VA_ARGS__)

static const fmed_core *core;

typedef struct fmed_caf {
	ffcaf caf;
	uint state;
} fmed_caf;


//FMEDIA MODULE
static const void* cafmod_iface(const char *name);
static int cafmod_sig(uint signo);
static void cafmod_destroy(void);
static const fmed_mod fmed_caf_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&cafmod_iface, &cafmod_sig, &cafmod_destroy
};

//INPUT
static void* caf_open(fmed_filt *d);
static void caf_close(void *ctx);
static int caf_process(void *ctx, fmed_filt *d);
static const fmed_filter fmed_caf_input = {
	&caf_open, &caf_process, &caf_close
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_caf_mod;
}


static const void* cafmod_iface(const char *name)
{
	if (!ffsz_cmp(name, "in"))
		return &fmed_caf_input;
	return NULL;
}

static int cafmod_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;
	}
	return 0;
}

static void cafmod_destroy(void)
{
}


static void* caf_open(fmed_filt *d)
{
	fmed_caf *c = ffmem_tcalloc1(fmed_caf);
	if (c == NULL) {
		errlog(d->trk, "%s", ffmem_alloc_S);
		return NULL;
	}
	ffcaf_open(&c->caf);
	return c;
}

static void caf_close(void *ctx)
{
	fmed_caf *c = ctx;
	ffcaf_close(&c->caf);
	ffmem_free(c);
}

static void caf_meta(fmed_caf *c, fmed_filt *d)
{
	d->track->meta_set(d->trk, &c->caf.tagname, &c->caf.tagval, FMED_QUE_TMETA);
}

static const byte caf_codecs[] = {
	FFCAF_AAC, FFCAF_ALAC,
};
static const char* const caf_codecs_str[] = {
	"aac.decode", "alac.decode",
};

static int caf_process(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA };
	fmed_caf *c = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		ffcaf_input(&c->caf, d->data, d->datalen);
		d->datalen = 0;
	}

	switch (c->state) {
	case I_HDR:
		break;

	case I_DATA:
		break;
	}

	if (d->flags & FMED_FLAST)
		ffcaf_fin(&c->caf);

	for (;;) {
		r = ffcaf_read(&c->caf);
		switch (r) {
		case FFCAF_MORE:
			if (d->flags & FMED_FLAST) {
				errlog(d->trk, "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFCAF_DONE:
			d->outlen = 0;
			return FMED_RDONE;

		case FFCAF_DATA:
			goto data;

		case FFCAF_HDR: {
			dbglog(d->trk, "packets:%U  frames/packet:%u  bytes/packet:%u"
				, c->caf.info.total_packets, c->caf.info.packet_frames, c->caf.info.packet_bytes);

			int i = ffint_find1(caf_codecs, FFCNT(caf_codecs), c->caf.info.format);
			if (i == -1) {
				errlog(d->trk, "unsupported codec: %xu", c->caf.info.format);
				return FMED_RERR;
			}

			const char *codec = caf_codecs_str[i];
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)codec)) {
				return FMED_RERR;
			}
			d->audio.fmt.channels = c->caf.info.pcm.channels;
			d->audio.fmt.sample_rate = c->caf.info.pcm.sample_rate;
			d->audio.total = c->caf.info.total_frames;
			d->audio.bitrate = c->caf.info.bitrate;

			if (d->input_info)
				return FMED_RDONE;

			ffstr asc = ffcaf_asc(&c->caf);
			d->out = asc.ptr,  d->outlen = asc.len;
			c->state = I_DATA;
			return FMED_RDATA;
		}

		case FFCAF_TAG:
			caf_meta(c, d);
			break;

		case FFCAF_SEEK:
			d->input.seek = ffcaf_seekoff(&c->caf);
			return FMED_RMORE;

		case FFCAF_ERR:
		default:
			errlog(d->trk, "ffcaf_read(): %s", ffcaf_errstr(&c->caf));
			return FMED_RERR;
		}
	}

data:
	d->audio.pos = ffcaf_cursample(&c->caf);
	ffstr out = ffcaf_output(&c->caf);
	d->out = out.ptr,  d->outlen = out.len;
	return FMED_RDATA;
}
