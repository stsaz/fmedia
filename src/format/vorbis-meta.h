/** fmedia: Vorbis meta read
2022, Simon Zolin */

#include <fmedia.h>
#include <avpack/vorbistag.h>

enum VORBIS_HDR_T {
	VORBIS_HDR_INFO = 1,
	VORBIS_HDR_COMMENT = 3,
};

struct vorbis_hdr {
	ffbyte type; // enum VORBIS_HDR_T
	char vorbis[6]; // ="vorbis"
};

struct vorbis_info {
	ffbyte ver[4]; // =0
	ffbyte channels;
	ffbyte rate[4];
	ffbyte br_max[4];
	ffbyte br_nominal[4];
	ffbyte br_min[4];
	ffbyte blocksize;
	ffbyte framing_bit; // =1
};

/** Read Vorbis-info packet */
static int vorbisinfo_read(ffstr pkt, ffuint *channels, ffuint *rate, ffuint *br_nominal)
{
	const struct vorbis_hdr *h = (struct vorbis_hdr*)pkt.ptr;
	if (sizeof(struct vorbis_hdr) + sizeof(struct vorbis_info) > pkt.len
		|| !(h->type == VORBIS_HDR_INFO && !ffmem_cmp(h->vorbis, "vorbis", 6)))
		return -1;

	const struct vorbis_info *vi = (struct vorbis_info*)(pkt.ptr + sizeof(struct vorbis_hdr));
	*channels = vi->channels;
	*rate = ffint_le_cpu32_ptr(vi->rate);
	*br_nominal = ffint_le_cpu32_ptr(vi->br_nominal);
	if (0 != ffint_le_cpu32_ptr(vi->ver)
		|| *channels == 0
		|| *rate == 0
		|| vi->framing_bit != 1)
		return -2;

	return 0;
}

/** Check Vorbis-comment packet.
body: Vorbis-tag data */
static int vorbiscomment_read(ffstr pkt, ffstr *body)
{
	const struct vorbis_hdr *h = (struct vorbis_hdr*)pkt.ptr;
	if (sizeof(struct vorbis_hdr) > pkt.len
		|| !(h->type == VORBIS_HDR_COMMENT && !ffmem_cmp(h->vorbis, "vorbis", 6)))
		return -1;

	*body = pkt;
	ffstr_shift(body, sizeof(struct vorbis_hdr));
	return 0;
}


struct vorbismeta {
	uint state;
};

static void* vorbismeta_open(fmed_track_info *d)
{
	struct vorbismeta *v = ffmem_new(struct vorbismeta);
	return v;
}

static void vorbismeta_close(void *ctx)
{
	struct vorbismeta *v = ctx;
	ffmem_free(v);
}

int vorbistag_read(fmed_track_info *d, ffstr vc)
{
	vorbistagread vtag = {};
	for (;;) {
		ffstr name, val;
		int tag = vorbistagread_process(&vtag, &vc, &name, &val);
		switch (tag) {
		case VORBISTAGREAD_DONE:
			return 0;
		case VORBISTAGREAD_ERROR:
			errlog1(d->trk, "vorbistagread_process");
			return -1;
		}

		dbglog1(d->trk, "%S: %S", &name, &val);
		if (tag != 0)
			ffstr_setz(&name, ffmmtag_str[tag]);
		d->track->meta_set(d->trk, &name, &val, FMED_QUE_TMETA);
	}
}

static int vorbismeta_read(void *ctx, fmed_track_info *d)
{
	int r = FMED_ROK;
	struct vorbismeta *v = ctx;
	switch (v->state) {
	case 0: {
		uint chan, rate, br;
		if (0 != vorbisinfo_read(d->data_in, &chan, &rate, &br)) {
			errlog1(d->trk, "vorbisinfo_read");
			break;
		}
		d->audio.decoder = "Vorbis";
		d->audio.fmt.format = FFPCM_FLOAT;
		d->audio.fmt.channels = chan;
		d->audio.fmt.sample_rate = rate;
		d->audio.bitrate = br;
		if (d->stream_copy)
			d->meta_block = 1;
		v->state = 1;
		break;
	}

	case 1: {
		r = FMED_RDONE;
		if (d->input_info)
			r = FMED_RLASTOUT;
		v->state = 2;
		ffstr vc;
		if (0 != vorbiscomment_read(d->data_in, &vc)) {
			errlog1(d->trk, "vorbiscomment_read");
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

const fmed_filter vorbismeta_input = {
	vorbismeta_open, vorbismeta_read, vorbismeta_close
};
