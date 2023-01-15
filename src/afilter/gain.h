/** fmedia: soundmod: gain
2022, Simon Zolin */

struct gain {
	ffpcmex pcm;
	uint samp_size;
	int db;
	double gain;
};

static void* sndmod_gain_open(fmed_filt *d)
{
	struct gain *c = ffmem_new(struct gain);
	if (c == NULL)
		return NULL;
	c->pcm = d->audio.fmt;
	c->samp_size = ffpcm_size1(&c->pcm);
	c->db = ~d->audio.gain;
	return c;
}

static void sndmod_gain_close(void *ctx)
{
	struct gain *c = ctx;
	ffmem_free(c);
}

static int sndmod_gain_process(void *ctx, fmed_filt *d)
{
	struct gain *c = ctx;
	int db = d->audio.gain;
	if (db != FMED_NULL && db != 0) {
		if (db != c->db) {
			c->db = db;
			c->gain = ffpcm_db2gain((double)db / 100);
		}
		ffpcm_gain(&c->pcm, c->gain, d->data_in.ptr, (void*)d->data_in.ptr, d->data_in.len / c->samp_size);
	}

	d->data_out = d->data_in;
	d->data_in.len = 0;
	if (d->flags & FMED_FLAST)
		return FMED_RDONE;
	return FMED_ROK;
}

const fmed_filter fmed_sndmod_gain = {
	sndmod_gain_open, sndmod_gain_process, sndmod_gain_close
};
