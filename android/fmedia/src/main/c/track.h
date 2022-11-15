/** fmedia/Android: track manager
2022, Simon Zolin */

struct filter {
	const char *name;
	const fmed_filter *iface;
	void *obj;
	uint backward_skip :1;
};

/* Max filter chain:
ifile->detector->ifmt->meta->ctl */
#define MAX_FILTERS 5

struct track_ctx {
	fmed_track_info ti;

	struct filter filters_pool[MAX_FILTERS];
	uint i_fpool;
	struct filter *filters_active[MAX_FILTERS];
	ffslice filters;
	uint cur;
};

const fmed_track track_iface;

static fmed_track_obj* trk_create(uint cmd, const char *url)
{
	struct track_ctx *t = ffmem_new(struct track_ctx);
	t->cur = -1;
	t->ti.trk = t;
	t->ti.track = &track_iface;
	t->ti.input.seek = FMED_NULL;
	t->ti.audio.decoder = "";
	t->filters.ptr = t->filters_active;
	return t;
}

static void trk_close(struct track_ctx *t)
{
	if (t == NULL) return;

	for (uint i = 0;  i != MAX_FILTERS;  i++) {
		const struct filter *f = &t->filters_pool[i];
		if (f->obj != NULL) {
			dbglog1(t, "%s: closing filter", f->name);
			f->iface->close(f->obj);
		}
	}

	char **ps;
	FFSLICE_WALK(&t->ti.meta, ps) {
		ffmem_free(*ps);
	}
	ffvec_free(&t->ti.meta);

	dbglog1(t, "track closed");
	ffmem_free(t);
}

static fmed_track_info* trk_conf(fmed_track_obj *trk)
{
	struct track_ctx *t = trk;
	return &t->ti;
}

static void trk_process(struct track_ctx *t)
{
	uint i = 0;
	int r;
	for (;;) {
		struct filter *f = *ffslice_itemT(&t->filters, i, struct filter*);

		if (f->backward_skip) {
			// last time the filter returned FMED_ROK
			f->backward_skip = 0;
			if (i != 0) {
				// go to previous filter
				r = FMED_RMORE;
				goto result;
			}
			// calling first-in-chain filter
		}

		t->ti.flags &= ~FMED_FFIRST;
		if (i == 0)
			t->ti.flags |= FMED_FFIRST;

		t->cur = i;
		if (f->obj == NULL) {
			dbglog1(t, "%s: opening filter", f->name);
			if (NULL == (f->obj = f->iface->open(&t->ti))) {
				dbglog1(t, "%s: filter open failed", f->name);
				goto err;
			}
			FF_ASSERT(f->obj != FMED_FILT_SKIP);
		}

		dbglog1(t, "%s: calling filter, input:%L  flags:%xu"
			, f->name, t->ti.data_in.len, t->ti.flags);
		r = f->iface->process(f->obj, &t->ti);

		static const char rc_str[][16] = {
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
		dbglog1(t, "%s: filter returned %s, output:%L"
			, f->name, rc_str[r+1], t->ti.data_out.len);

result:
		switch (r) {
		case FMED_RDONE:
		case FMED_RLASTOUT:
			if (r == FMED_RDONE) {
				// deactivate filter
				ffslice_rmT(&t->filters, i, 1, void*);
				i--;
			} else {
				// deactivate this and all previous filters
				ffslice_rmT(&t->filters, 0, i + 1, void*);
				i = -1;
			}
			if (t->filters.len == 0) {
				// all filters have finished
				goto err;
			}
			goto go_fwd;

		case FMED_ROK:
			f->backward_skip = 1;
			// fallthrough
		case FMED_RDATA:
go_fwd:
			if (i + 1 == t->filters.len) {
				// last-in-chain filter returned data
				goto go_back;
			}
			i++;

			t->ti.flags |= FMED_FFWD;
			t->ti.data_in = t->ti.data_out;
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RMORE:
go_back:
			if (i == 0) {
				errlog1(t, "%s: first-in-chain filter wants more data", f->name);
				goto err;
			}
			i--;
			t->ti.flags &= ~FMED_FFWD;
			ffstr_null(&t->ti.data_in);
			ffstr_null(&t->ti.data_out);
			break;

		case FMED_RFIN:
		case FMED_RERR:
			goto err;

		default:
			errlog1(t, "%s: bad return code %u", f->name, r);
			goto err;
		}
	}

err:
	dbglog1(t, "finished processing");
}

static char* trk_chain_print(struct track_ctx *t, const struct filter *mark, char *buf, ffsize cap)
{
	cap--;
	ffstr s = FFSTR_INITN(buf, 0);
	const struct filter **pf, *f;
	FFSLICE_WALK(&t->filters, pf) {
		f =  *pf;
		if (f == mark)
			ffstr_addchar(&s, cap, '*');
		ffstr_addfmt(&s, cap, "%s -> ", f->name);
	}
	s.ptr[s.len] = '\0';
	return buf;
}

/**
Return filter index within chain */
static int trk_filter_add(struct track_ctx *t, const char *name, const fmed_filter *iface)
{
	if (t->i_fpool == MAX_FILTERS) {
		errlog1(t, "max filters limit reached");
		return -1;
	}

	struct filter *f = t->filters_pool + t->i_fpool;
	t->i_fpool++;
	f->iface = iface;
	f->name = name;

	t->filters.len++;
	struct filter **pf;
	if ((int)t->cur < 0 || t->cur + 1 == t->filters.len)
		pf = ffslice_lastT(&t->filters, struct filter*);
	else
		pf = ffslice_moveT(&t->filters, t->cur + 1, t->cur + 2, t->filters.len - 1 - (t->cur + 1), struct filter*);
	*pf = f;

	char buf[200];
	dbglog1(t, "%s: added to chain [%s]"
		, f->name, trk_chain_print(t, f, buf, sizeof(buf)));
	return pf - (struct filter**)t->filters.ptr;
}

extern const fmed_filter* core_getfilter(const char *name);

static ffssize trk_cmd(void *trk, uint cmd, ...)
{
	struct track_ctx *t = trk;
	dbglog1(t, "%s: %u", __func__, cmd);

	ffssize r = -1;
	va_list va;
	va_start(va, cmd);

	switch (cmd) {

	case FMED_TRACK_START:
		trk_process(t);
		r = 0;
		break;

	case FMED_TRACK_STOP:
		trk_close(t);
		r = 0;
		break;

	case FMED_TRACK_FILT_ADD: {
		const char *name = va_arg(va, void*);
		const fmed_filter *fi = (fmed_filter*)core->cmd(FMED_FILTER_BYNAME, name);
		if (fi == NULL) {
			r = 0;
			break;
		}
		r = trk_filter_add(t, name, fi);
		r++;
		break;
	}

	default:
		errlog1(t, "%s: bad command %u", __func__, cmd);
	}

	va_end(va);
	return r;
}

static void trk_meta_set(void *trk, const ffstr *name, const ffstr *val, uint flags)
{
	struct track_ctx *t = trk;
	*ffvec_pushT(&t->ti.meta, char*) = ffsz_dupstr(name);
	*ffvec_pushT(&t->ti.meta, char*) = ffsz_dupstr(val);
}

const fmed_track track_iface = {
	.create = trk_create,
	.conf = trk_conf,
	.cmd = trk_cmd,
	.meta_set = trk_meta_set,
};

#undef MAX_FILTERS
