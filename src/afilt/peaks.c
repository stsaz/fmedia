/** Analyze and print audio peaks information.
Copyright (c) 2019 Simon Zolin */

#include <fmedia.h>


extern const fmed_core *core;

// PEAKS
static void* sndmod_peaks_open(fmed_filt *d);
static int sndmod_peaks_process(void *ctx, fmed_filt *d);
static void sndmod_peaks_close(void *ctx);
const fmed_filter fmed_sndmod_peaks = {
	&sndmod_peaks_open, &sndmod_peaks_process, &sndmod_peaks_close
};


typedef struct sndmod_peaks {
	uint state;
	uint nch;
	uint64 total;

	struct {
		uint crc;
		uint high;
		uint64 sum;
		uint64 clipped;
	} ch[8];
	uint do_crc :1;
} sndmod_peaks;

static void* sndmod_peaks_open(fmed_filt *d)
{
	sndmod_peaks *p = ffmem_tcalloc1(sndmod_peaks);
	if (p == NULL)
		return NULL;

	p->nch = d->audio.convfmt.channels;
	if (p->nch > FFCNT(p->ch)) {
		ffmem_free(p);
		return NULL;
	}

	p->do_crc = d->pcm_peaks_crc;
	return p;
}

static void sndmod_peaks_close(void *ctx)
{
	sndmod_peaks *p = ctx;
	ffmem_free(p);
}

static int sndmod_peaks_process(void *ctx, fmed_filt *d)
{
	sndmod_peaks *p = ctx;
	size_t i, ich, samples;

	switch (p->state) {
	case 0:
		d->audio.convfmt.ileaved = 0;
		d->audio.convfmt.format = FFPCM_16LE;
		p->state = 1;
		return FMED_RMORE;

	case 1:
		if (d->audio.convfmt.ileaved
			|| d->audio.convfmt.format != FFPCM_16LE) {
			errlog(core, d->trk, "peaks", "input must be non-interleaved 16LE PCM");
			return FMED_RERR;
		}
		p->state = 2;
		break;
	}

	samples = d->datalen / (sizeof(short) * p->nch);
	p->total += samples;

	for (ich = 0;  ich != p->nch;  ich++) {
		for (i = 0;  i != samples;  i++) {
			int sh = ((short**)d->datani)[ich][i];

			if (sh == 0x7fff || sh == -0x8000)
				p->ch[ich].clipped++;

			if (sh < 0)
				sh = -sh;

			if (p->ch[ich].high < (uint)sh)
				p->ch[ich].high = sh;

			p->ch[ich].sum += sh;
		}

		if (p->do_crc)
			p->ch[ich].crc = crc32(d->datani[ich], d->datalen / p->nch, p->ch[ich].crc);
	}

	d->out = d->data;
	d->outlen = d->datalen;
	d->datalen = 0;

	if (d->flags & FMED_FLAST) {
		ffstr3 buf = {0};
		ffstr_catfmt(&buf, FF_NEWLN "PCM peaks (%,U total samples):" FF_NEWLN
			, p->total);

		if (p->total != 0) {
			for (ich = 0;  ich != p->nch;  ich++) {

				double hi = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].high));
				double avg = ffpcm_gain2db(_ffpcm_16le_flt(p->ch[ich].sum / p->total));
				ffstr_catfmt(&buf, "Channel #%L: highest peak:%.2FdB, avg peak:%.2FdB.  Clipped: %U (%.4F%%).  CRC:%08xu" FF_NEWLN
					, ich + 1, hi, avg
					, p->ch[ich].clipped, ((double)p->ch[ich].clipped * 100 / p->total)
					, p->ch[ich].crc);
			}
		}

		core->log(FMED_LOG_USER, d->trk, NULL, "%S", &buf);
		ffarr_free(&buf);
		return FMED_RDONE;
	}
	return FMED_ROK;
}
