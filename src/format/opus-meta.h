/** fmedia: Opus meta read
2022, Simon Zolin */

#include <fmedia.h>
#include <avpack/vorbistag.h>

extern int vorbistag_read(fmed_track_info *d, ffstr vc);

struct opus_hdr {
	char id[8]; // ="OpusHead"
	ffbyte ver;
	ffbyte channels;
	ffbyte preskip[2];
	ffbyte orig_sample_rate[4];
	//ffbyte unused[3];
};

struct opus_info {
	ffuint channels;
	ffuint rate;
	ffuint orig_rate;
	ffuint preskip;
};

/** Read OpusHead packet */
static int opusinfo_read(struct opus_info *i, ffstr pkt)
{
	const struct opus_hdr *h = (struct opus_hdr*)pkt.ptr;
	if (sizeof(struct opus_hdr) > pkt.len
		|| !!ffmem_cmp(h->id, "OpusHead", 8)
		|| h->ver != 1)
		return -1;

	i->channels = h->channels;
	i->rate = 48000;
	i->orig_rate = ffint_le_cpu32_ptr(h->orig_sample_rate);
	i->preskip = ffint_le_cpu16_ptr(h->preskip);
	return 0;
}

/** Check OpusTags packet.
body: Vorbis-tag data */
static int opuscomment_read(ffstr pkt, ffstr *body)
{
	if (8 > pkt.len
		|| ffmem_cmp(pkt.ptr, "OpusTags", 8))
		return -1;
	ffstr_set(body, pkt.ptr + 8, pkt.len - 8);
	return 0;
}


struct opusmeta {
	uint state;
};

static void* opusmeta_open(fmed_track_info *d)
{
	struct opusmeta *o = ffmem_new(struct opusmeta);
	return o;
}

static void opusmeta_close(void *ctx)
{
	struct opusmeta *o = ctx;
	ffmem_free(o);
}

static int opusmeta_read(void *ctx, fmed_track_info *d)
{
	int r = FMED_ROK;
	struct opusmeta *o = ctx;
	switch (o->state) {
	case 0: {
		struct opus_info info;
		if (0 != opusinfo_read(&info, d->data_in)) {
			errlog1(d->trk, "opusinfo_read");
			break;
		}
		d->audio.decoder = "Opus";
		d->audio.fmt.format = FFPCM_FLOAT;
		d->audio.fmt.channels = info.channels;
		d->audio.fmt.sample_rate = info.rate;
		if (d->stream_copy)
			d->meta_block = 1;
		o->state = 1;
		break;
	}

	case 1: {
		r = FMED_RDONE;
		if (d->input_info)
			r = FMED_RLASTOUT;
		o->state = 2;
		ffstr vc;
		if (0 != opuscomment_read(d->data_in, &vc)) {
			errlog1(d->trk, "opuscomment_read");
			break;
		}
		vorbistag_read(d, vc);
		break;
	}
	}

	d->data_out = d->data_in;
	d->data_in.len = 0;
	return r;
}

const fmed_filter opusmeta_input = {
	opusmeta_open, opusmeta_read, opusmeta_close
};
