/** fmedia/Android: track manager
2022, Simon Zolin */

struct filter {
	const fmed_filter *iface;
	void *obj;
	uint done :1;
	uint first :1;
};

struct track_ctx {
	fmed_track_info ti;
	struct filter filters[2];
	ffvec meta; // {char*, char*}[]
};

static const fmed_track track_iface;

static struct track_ctx* trk_create()
{
	struct track_ctx *t = ffmem_new(struct track_ctx);
	t->ti.trk = t;
	t->ti.track = &track_iface;
	t->ti.input.seek = FMED_NULL;
	return t;
}

static void trk_free(struct track_ctx *t)
{
	if (t == NULL) return;

	for (uint i = 0;  i != FF_COUNT(t->filters);  i++) {
		const struct filter *f = &t->filters[i];
		if (f->obj != NULL) {
			dbglog1(t->ti.trk, "closing filter %u", i);
			f->iface->close(f->obj);
		}
	}

	char **ps;
	FFSLICE_WALK(&t->meta, ps) {
		ffmem_free(*ps);
	}
	ffvec_free(&t->meta);

	dbglog1(t->ti.trk, "track closed");
	ffmem_free(t);
}

static void trk_process(struct track_ctx *t)
{
	uint i = 0;
	for (;;) {
		struct filter *f = &t->filters[i];

		t->ti.flags &= ~FMED_FLAST;
		if (f->first)
			t->ti.flags |= FMED_FLAST;

		if (f->obj == NULL) {
			if (NULL == (f->obj = f->iface->open(&t->ti)))
				return;
			dbglog1(t->ti.trk, "opened filter %u", i);
		}

		int r = f->iface->process(f->obj, &t->ti);

		static const char retstr[][16] = {
			"FMED_RERR",
			"FMED_ROK",
			"FMED_RDATA",
			"FMED_RDONE",
			"FMED_RLASTOUT",
			"FMED_RNEXTDONE",
			"FMED_RMORE",
			"FMED_RBACK",
			"FMED_RASYNC",
			"FMED_RFIN",
			"FMED_RSYSERR",
			"FMED_RDONE_ERR",
		};
		dbglog1(t->ti.trk, "filter %u returned %s, output:%L"
			, i, retstr[r+1], t->ti.data_out.len);

		switch (r) {
		case FMED_RLASTOUT:
		case FMED_RDONE:
			f->done = 1;
			// fallthrough

		case FMED_RDATA:
		case FMED_ROK:
			if (i == FF_COUNT(t->filters) - 1)
				return;
			i++;

			if (f->first && f->done)
				t->filters[i].first = 1;

			t->ti.data_in = t->ti.data_out;
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RMORE:
			if (i == 0)
				return;
			if (f->first)
				return;
			i--;
			ffstr_null(&t->ti.data_in);
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RERR:
			return;
		}
	}
}

static ssize_t trk_cmd(void *trk, uint cmd, ...)
{
	return -1;
}

static void trk_meta_set(void *trk, const ffstr *name, const ffstr *val, uint flags)
{
	struct track_ctx *t = trk;
	*ffvec_pushT(&t->meta, char*) = ffsz_dupstr(name);
	*ffvec_pushT(&t->meta, char*) = ffsz_dupstr(val);
}

static const fmed_track track_iface = {
	.cmd = trk_cmd,
	.meta_set = trk_meta_set,
};
