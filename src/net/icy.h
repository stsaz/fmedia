/** fmedia: ICY protocol input
2016,2021, Simon Zolin */

#include <avpack/icy.h>

struct icy {
	fmed_filt *d;
	icyread icy;
	ffstr data;
	ffstr next_filt_ext;

	netin *netin;
	ffstr artist;
	ffstr title;

	uint out_copy :1;
	uint save_oncmd :1;
};

int icy_setmeta(icy *c, const ffstr *_data);

#define FILT_NAME  "net.icy"

const fmed_conf_arg icy_conf_args[] = {
	{ "meta",	FMC_BOOL8,  FMC_O(net_conf, meta) },
	{}
};

int icy_config(fmed_conf_ctx *ctx)
{
	net->conf.meta = 1;
	fmed_conf_addctx(ctx, &net->conf, icy_conf_args);
	return 0;
}

void* icy_open(fmed_filt *d)
{
	icy *c;
	if (NULL == (c = ffmem_new(icy)))
		return NULL;
	c->d = d;

	int v = net->track->getval(d->trk, "out-copy");
	c->out_copy = (v != FMED_NULL);
	c->save_oncmd = (v == FMED_OUTCP_CMD);

	const char *s = net->track->getvalstr(d->trk, "icy_format");
	ffstr_setz(&c->next_filt_ext, s);

	return c;
}

void icy_close(void *ctx)
{
	icy *c = ctx;
	ffstr_free(&c->artist);
	ffstr_free(&c->title);

	if (c->netin != NULL) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	ffmem_free(c);
}

/** Initialize after a new connection is established. */
int icy_reset(icy *c, fmed_filt *d)
{
	icyread_open(&c->icy, fmed_getval("icy_meta_int"));

	// "netin" may be initialized if we've reconnected after I/O failure
	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c, c->d);
	return FMED_RDATA;
}

int icy_process(void *ctx, fmed_filt *d)
{
	icy *c = ctx;
	int r;
	ffstr s;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	if (d->flags & FMED_FFWD) {
		if (d->net_reconnect) {
			d->net_reconnect = 0;
			if (FMED_RDATA != (r = icy_reset(c, d)))
				return r;
		}
		ffstr_set(&c->data, d->data, d->datalen);
		d->datalen = 0;
	}

	for (;;) {

		r = icyread_process(&c->icy, &c->data, &s);
		switch (r) {
		case ICYREAD_MORE:
			return FMED_RMORE;

		case ICYREAD_DATA:
			if (c->netin != NULL) {
				netin_write(c->netin, &s);
			}

			d->out = s.ptr;
			d->outlen = s.len;
			return FMED_RDATA;

		case ICYREAD_META:
			icy_setmeta(c, &s);
			break;
		}
	}
}

int icy_setmeta(icy *c, const ffstr *_data)
{
	ffstr artist = {0}, title = {0}, data = *_data, pair[2];
	fmed_que_entry *qent;

	dbglog(c->d->trk, "meta: [%L] %S", data.len, &data);

	for (;;) {
		ffstr k, v;
		if (0 != icymeta_read(&data, &k, &v))
			break;

		if (ffstr_ieqz(&k, "StreamTitle"))
			icymeta_artist_title(v, &artist, &title);
	}

	qent = (void*)c->d->track->getval(c->d->trk, "queue_item");
	ffarr utf = {0};

	if (ffutf8_valid(artist.ptr, artist.len))
		ffarr_append(&utf, artist.ptr, artist.len);
	else
		ffstr_growadd_codepage((ffstr*)&utf, &utf.cap, artist.ptr, artist.len, FFUNICODE_WIN1252);
	ffstr_free(&c->artist);
	ffstr_acqstr3(&c->artist, &utf);

	ffarr_null(&utf);
	if (ffutf8_valid(title.ptr, title.len))
		ffarr_append(&utf, title.ptr, title.len);
	else
		ffstr_growadd_codepage((ffstr*)&utf, &utf.cap, title.ptr, title.len, FFUNICODE_WIN1252);
	ffstr_free(&c->title);
	ffstr_acqstr3(&c->title, &utf);

	ffstr_setcz(&pair[0], "artist");
	ffstr_set2(&pair[1], &c->artist);
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);

	ffstr_setcz(&pair[0], "title");
	ffstr_set2(&pair[1], &c->title);
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);

	c->d->meta_changed = 1;

	if (c->netin != NULL && c->netin->fn_dyn) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c, c->d);
	return 0;
}

const fmed_filter fmed_icy = {
	icy_open, icy_process, icy_close
};

#undef FILT_NAME
