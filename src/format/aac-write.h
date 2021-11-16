/** fmedia: AAC ADTS (.aac) writer
2019, Simon Zolin */

struct aac_adts_out {
	uint state;
};

static void* aac_adts_out_open(fmed_filt *d)
{
	struct aac_adts_out *a;
	if (NULL == (a = ffmem_new(struct aac_adts_out)))
		return NULL;
	return a;
}

static void aac_adts_out_close(void *ctx)
{
	struct aac_adts_out *a = ctx;
	ffmem_free(a);
}

static int aac_adts_out_process(void *ctx, fmed_filt *d)
{
	struct aac_adts_out *a = ctx;

	switch (a->state) {
	case 0:
		if (!ffsz_eq(d->datatype, "aac")) {
			errlog1(d->trk, "unsupported data type: %s", d->datatype);
			return FMED_RERR;
		}
		// if (d->datalen != 0) {
		// skip ASC
		// }
		a->state = 1;
		return FMED_RMORE;
	case 1:
		break;
	}

	if (d->datalen == 0 && !(d->flags & FMED_FLAST))
		return FMED_RMORE;
	d->out = d->data,  d->outlen = d->datalen;
	d->datalen = 0;
	return (d->flags & FMED_FLAST) ? FMED_RDONE : FMED_RDATA;
}

const fmed_filter aac_adts_output = {
	aac_adts_out_open, aac_adts_out_process, aac_adts_out_close
};
