/** MP4 input/output.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/data/mp4.h>
#include <FF/data/mmtag.h>


static const fmed_core *core;
static const fmed_queue *qu;


typedef struct mp4 {
	ffmp4 mp;
	uint state;
	uint seeking :1;
} mp4;

typedef struct mp4_out {
	uint state;
	ffmp4_cook mp;
} mp4_out;


//FMEDIA MODULE
static const void* mp4_iface(const char *name);
static int mp4_sig(uint signo);
static void mp4_destroy(void);
static const fmed_mod fmed_mp4_mod = {
	&mp4_iface, &mp4_sig, &mp4_destroy
};

//INPUT
static void* mp4_in_create(fmed_filt *d);
static void mp4_in_free(void *ctx);
static int mp4_in_decode(void *ctx, fmed_filt *d);
static const fmed_filter fmed_mp4_input = {
	&mp4_in_create, &mp4_in_decode, &mp4_in_free
};

//OUTPUT
static void* mp4_out_create(fmed_filt *d);
static void mp4_out_free(void *ctx);
static int mp4_out_encode(void *ctx, fmed_filt *d);
static const fmed_filter mp4_output = {
	&mp4_out_create, &mp4_out_encode, &mp4_out_free
};

static void mp4_meta(mp4 *m, fmed_filt *d);
static int mp4_out_addmeta(mp4_out *m, fmed_filt *d);


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_mp4_mod;
}


static const void* mp4_iface(const char *name)
{
	if (!ffsz_cmp(name, "input"))
		return &fmed_mp4_input;
	else if (!ffsz_cmp(name, "output"))
		return &mp4_output;
	return NULL;
}

static int mp4_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		return 0;

	case FMED_OPEN:
		qu = core->getmod("#queue.queue");
		break;
	}
	return 0;
}

static void mp4_destroy(void)
{
}


static void* mp4_in_create(fmed_filt *d)
{
	mp4 *m = ffmem_tcalloc1(mp4);
	if (m == NULL)
		return NULL;

	ffmp4_init(&m->mp);

	if ((int64)d->input.size != FMED_NULL)
		m->mp.total_size = d->input.size;

	return m;
}

static void mp4_in_free(void *ctx)
{
	mp4 *m = ctx;
	ffmp4_close(&m->mp);
	ffmem_free(m);
}

static void mp4_meta(mp4 *m, fmed_filt *d)
{
	ffstr name, val;
	if (m->mp.tag == 0)
		return;
	ffstr_setz(&name, ffmmtag_str[m->mp.tag]);
	val = m->mp.tagval;

	dbglog(core, d->trk, "mp4", "tag: %S: %S", &name, &val);

	qu->meta_set((void*)fmed_getval("queue_item"), name.ptr, name.len, val.ptr, val.len, FMED_QUE_TMETA);
}

static int mp4_in_decode(void *ctx, fmed_filt *d)
{
	enum { I_HDR, I_DATA1, I_DATA, };
	mp4 *m = ctx;
	int r;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	m->mp.data = d->data;
	m->mp.datalen = d->datalen;

	for (;;) {

	switch (m->state) {

	case I_DATA1:
	case I_DATA:
		if ((int64)d->audio.seek != FMED_NULL && !m->seeking) {
			m->seeking = 1;
			uint64 seek = ffpcm_samples(d->audio.seek, m->mp.fmt.sample_rate);
			ffmp4_seek(&m->mp, seek);
		}
		if (m->state == I_DATA1) {
			m->state = I_DATA;
			return FMED_RDATA;
		}

	case I_HDR:
		r = ffmp4_read(&m->mp);
		switch (r) {
		case FFMP4_RMORE:
			if (d->flags & FMED_FLAST) {
				warnlog(core, d->trk, "mp4", "file is incomplete");
				d->outlen = 0;
				return FMED_RDONE;
			}
			return FMED_RMORE;

		case FFMP4_RHDR: {
			ffpcm_fmtcopy(&d->audio.fmt, &m->mp.fmt);

			d->audio.total = ffmp4_totalsamples(&m->mp);

			const char *filt;
			if (m->mp.codec == FFMP4_ALAC) {
				filt = "alac.decode";
				d->audio.bitrate = ffmp4_bitrate(&m->mp);
			}
			else if (m->mp.codec == FFMP4_AAC) {
				filt = "aac.decode";
				fmed_setval("audio_enc_delay", m->mp.enc_delay);
				fmed_setval("audio_end_padding", m->mp.end_padding);
				d->audio.bitrate = (m->mp.aac_brate != 0) ? m->mp.aac_brate : ffmp4_bitrate(&m->mp);

			} else if (m->mp.codec == FFMP4_MPEG1) {
				filt = "mpeg.decode";
				d->audio.bitrate = (m->mp.aac_brate != 0) ? m->mp.aac_brate : 0;

			} else {
				errlog(core, d->trk, "mp4", "%s: decoding unsupported", ffmp4_codec(m->mp.codec));
				return FMED_RERR;
			}
			if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT, (void*)filt))
				return FMED_RERR;
			d->data = m->mp.data,  d->datalen = m->mp.datalen;
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			m->state = I_DATA1;
			continue;
		}

		case FFMP4_RTAG:
			mp4_meta(m, d);
			break;

		case FFMP4_RMETAFIN:
			if (d->input_info)
				return FMED_ROK;

			m->state = I_DATA;
			continue;

		case FFMP4_RDATA:
			d->audio.pos = ffmp4_cursample(&m->mp);
			dbglog(core, d->trk, NULL, "passing %L bytes at position #%U"
				, m->mp.outlen, d->audio.pos);
			d->data = m->mp.data,  d->datalen = m->mp.datalen;
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			m->seeking = 0;
			return FMED_RDATA;

		case FFMP4_RDONE:
			d->outlen = 0;
			return FMED_RLASTOUT;

		case FFMP4_RSEEK:
			d->input.seek = m->mp.off;
			return FMED_RMORE;

		case FFMP4_RWARN:
			warnlog(core, d->trk, "mp4", "ffmp4_read(): at offset 0x%xU: %s"
				, m->mp.off, ffmp4_errstr(&m->mp));
			break;

		case FFMP4_RERR:
			errlog(core, d->trk, "mp4", "ffmp4_read(): %s", ffmp4_errstr(&m->mp));
			return FMED_RERR;
		}
		break;
	}

	}

	//unreachable
}


static void* mp4_out_create(fmed_filt *d)
{
	mp4_out *m = ffmem_tcalloc1(mp4_out);
	if (m == NULL)
		return NULL;
	return m;
}

static void mp4_out_free(void *ctx)
{
	mp4_out *m = ctx;
	ffmp4_wclose(&m->mp);
	ffmem_free(m);
}

static int mp4_out_addmeta(mp4_out *m, fmed_filt *d)
{
	uint i;
	ffstr name, *val;
	void *qent;

	if (FMED_PNULL == (qent = (void*)fmed_getval("queue_item")))
		return 0;

	for (i = 0;  NULL != (val = qu->meta(qent, i, &name, FMED_QUE_UNIQ));  i++) {
		if (val == FMED_QUE_SKIP
			|| ffstr_eqcz(&name, "vendor"))
			continue;

		int tag;
		if (-1 == (tag = ffs_findarrz(ffmmtag_str, FFCNT(ffmmtag_str), name.ptr, name.len))) {
			warnlog(core, d->trk, "mp4", "unsupported tag: %S", &name);
			continue;
		}

		if (0 != ffmp4_addtag(&m->mp, tag, val->ptr, val->len)) {
			warnlog(core, d->trk, "mp4", "can't add tag: %S", &name);
		}
	}
	return 0;
}

/* Encoding process:
. Add encoder filter to the chain
. Get encoder config data
. Initialize MP4 output
. Wrap encoded audio data into MP4 */
static int mp4_out_encode(void *ctx, fmed_filt *d)
{
	mp4_out *m = ctx;
	int r;

	enum { I_INIT_ENC, I_INIT, I_MP4 };

	switch (m->state) {

	case I_INIT_ENC:
		if (0 != d->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, "aac.encode"))
			return FMED_RERR;
		m->state = I_INIT;
		return FMED_RMORE;

	case I_INIT: {
		ffstr asc;
		ffstr_set(&asc, d->data, d->datalen);
		d->datalen = 0;
		ffpcm fmt;
		ffpcm_fmtcopy(&fmt, &d->audio.convfmt);

		if ((int64)d->audio.total != FMED_NULL)
			m->mp.info.total_samples = ((d->audio.total - d->audio.pos) * d->audio.convfmt.sample_rate / d->audio.fmt.sample_rate);
		m->mp.info.frame_samples = fmed_getval("audio_frame_samples");
		m->mp.info.enc_delay = fmed_getval("audio_enc_delay");
		m->mp.info.bitrate = fmed_getval("audio_bitrate");
		if (0 != (r = ffmp4_create_aac(&m->mp, &fmt, &asc))) {
			errlog(core, d->trk, NULL, "ffmp4_create_aac(): %s", ffmp4_werrstr(&m->mp));
			return FMED_RERR;
		}

		if (0 != mp4_out_addmeta(m, d))
			return FMED_RERR;

		d->output.size = ffmp4_wsize(&m->mp);

		m->state = I_MP4;
		// break
	}
	}

	if (d->flags & FMED_FLAST)
		m->mp.fin = 1;

	if (d->datalen != 0) {
		m->mp.data = d->data,  m->mp.datalen = d->datalen;
		d->datalen = 0;
	}

	for (;;) {
		r = ffmp4_write(&m->mp);
		switch (r) {
		case FFMP4_RMORE:
			return FMED_RMORE;

		case FFMP4_RSEEK:
			d->output.seek = m->mp.off;
			continue;

		case FFMP4_RDATA:
			d->out = m->mp.out,  d->outlen = m->mp.outlen;
			return FMED_RDATA;

		case FFMP4_RDONE:
			d->outlen = 0;
			core->log(FMED_LOG_INFO, d->trk, NULL, "MP4: frames:%u, overhead: %.2F%%"
				, m->mp.frameno
				, (double)m->mp.mp4_size * 100 / (m->mp.mp4_size + m->mp.mdat_size));
			return FMED_RDONE;

		case FFMP4_RWARN:
			warnlog(core, d->trk, NULL, "ffmp4_write(): %s", ffmp4_werrstr(&m->mp));
			continue;

		case FFMP4_RERR:
			errlog(core, d->trk, NULL, "ffmp4_write(): %s", ffmp4_werrstr(&m->mp));
			return FMED_RERR;
		}
	}
}
