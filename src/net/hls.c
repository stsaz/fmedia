/** HLS client.
Copyright (c) 2019 Simon Zolin */

#include <net/net.h>
#include <FF/data/m3u.h>
#include <FF/path.h>


//HLS
static void* hls_open(fmed_filt *d);
static void hls_close(void *ctx);
static int hls_process(void *ctx, fmed_filt *d);
const fmed_filter nethls = {
	&hls_open, &hls_process, &hls_close
};


#define FILT_NAME "net.hls"

struct hls {
	uint state;
	uint http_status;
	uint64 seq, m3u_seq;
	void *con;
	void *trk;
	const char *m3u_url;
	char *file_url;
	ffstr base_url;
	ffstr http_data;
	ffm3u m3u;
	ffarr qu; //ffstr[]
	ffstr file_ext;
	uint first :1;
	uint have_media_seq :1;
};

enum {
	HLS_GETM3U, HLS_PARSEM3U,
	HLS_GETTRACK, HLS_FILERESP, HLS_FILECONT,
	HLS_ERR
};

static void* hls_open(fmed_filt *d)
{
	struct hls *c;
	if (NULL == (c = ffmem_new(struct hls)))
		return NULL;
	c->m3u_url = d->track->getvalstr(d->trk, "input");
	ffpath_split2(c->m3u_url, ffsz_len(c->m3u_url), &c->base_url, NULL);
	ffm3u_init(&c->m3u);
	c->trk = d->trk;
	c->first = 1;
	return c;
}

static void hls_close(void *ctx)
{
	struct hls *c = ctx;
	ffstr_free(&c->file_ext);
	http_iface.close(c->con);
	ffm3u_close(&c->m3u);
	ffmem_free(c->file_url);

	ffstr *ps;
	FFARR_WALKT(&c->qu, ps, ffstr) {
		ffstr_free(ps);
	}

	ffmem_free(c);
}

/** HTTP handler. */
static void hls_httpsig(void *param)
{
	struct hls *c = param;
	ffhttp_response *resp;
	ffstr data;
	int r = http_iface.recv(c->con, &resp, &data);
	c->http_status = r;

	switch (r) {

	case FFHTTPCL_RESP: {
		if (resp->code != 200) {
			c->state = HLS_ERR;
			goto wake;
		}

		ffstr val;
		if (0 != ffhttp_findihdr(&resp->h, FFHTTP_CONTENT_TYPE, &val)) {
			if (c->state == HLS_PARSEM3U) {
				if (!ffstr_eqz(&val, "application/vnd.apple.mpegurl")) {
					errlog(c->trk, "unsupported Content-Type: %S", &val);
					c->state = HLS_ERR;
					goto wake;
				}
			}
		}
		break;
	}

	case FFHTTPCL_RESP_RECV:
		c->http_data = data;
		//fallthrough
	case FFHTTPCL_DONE:
		goto wake;
	}

	if (r < 0) {
		goto wake;
	}

	http_iface.send(c->con, NULL);
	return;

wake:
	net->track->cmd(c->trk, FMED_TRACK_WAKE);
}

/** HTTP logger. */
static void hls_log(void *udata, uint level, const char *fmt, ...)
{
	struct hls *c = udata;

	uint lev;
	switch (level & 0x0f) {
	case FFHTTPCL_LOG_ERR:
		lev = FMED_LOG_ERR; break;
	case FFHTTPCL_LOG_WARN:
		lev = FMED_LOG_WARN; break;
	case FFHTTPCL_LOG_USER:
		lev = FMED_LOG_USER; break;
	case FFHTTPCL_LOG_INFO:
		lev = FMED_LOG_INFO; break;
	case FFHTTPCL_LOG_DEBUG:
		lev = FMED_LOG_DEBUG; break;
	default:
		return;
	}
	if (level & FFHTTPCL_LOG_SYS)
		lev |= FMED_LOG_SYS;

	va_list va;
	va_start(va, fmt);
	core->logv(lev, c->trk, FILT_NAME, fmt, va);
	va_end(va);
}

/** Add an element to the queue. */
static int hls_list_add(struct hls *c, const ffstr *name, uint64 seq)
{
	if (seq < c->seq) {
		dbglog(c->trk, "skipping data file #%U", seq);
		return 0;
	}

	ffstr *ps = ffarr_pushgrowT(&c->qu, 4, ffstr);
	if (NULL == ffstr_alcopystr(ps, name))
		return -1;
	if (c->qu.len == 1)
		c->seq = seq;
	dbglog(c->trk, "added data file #%U: %S [%L]"
		, seq, name, c->qu.len);
	return 0;
}

/** Get the first element from the queue and remove it.
name: User must free the value with ffstr_free(). */
static int hls_list_pop(struct hls *c, ffstr *name)
{
	if (c->qu.len == 0)
		return -1;
	*name = *ffarr_itemT(&c->qu, 0, ffstr);
	_ffarr_rmleft(&c->qu, 1, sizeof(ffstr));
	dbglog(c->trk, "playing data file #%U: %S [%L]"
		, c->seq, name, c->qu.len);
	c->seq++;
	return 0;
}

static int hls_request(struct hls *c, const char *url)
{
	FF_ASSERT(c->con == NULL);
	if (NULL == (c->con = http_iface.request("GET", url, 0)))
		return FMED_RERR;

	struct ffhttpcl_conf conf;
	http_iface.conf(c->con, &conf, FFHTTPCL_CONF_GET);
	conf.kq = (fffd)net->track->cmd(c->trk, FMED_TRACK_KQ);
	conf.log = &hls_log;
	http_iface.conf(c->con, &conf, FFHTTPCL_CONF_SET);

	if (net->conf.user_agent != 0) {
		ffstr s;
		ffstr_setz(&s, http_ua[net->conf.user_agent - 1]);
		http_iface.header(c->con, &ffhttp_shdr[FFHTTP_USERAGENT], &s, 0);
	}

	http_iface.sethandler(c->con, &hls_httpsig, c);
	http_iface.send(c->con, NULL);
	return 0;
}

/** Parse a chunk of .m3u data and add elements to the queue.
Return FMED_RMORE or an error. */
static int hls_m3u_parse(struct hls *c, const ffstr *data)
{
	ffstr d = *data;

	for (;;) {

		int r = ffm3u_parse(&c->m3u, &d);

		switch (r) {
		case FFM3U_URL: {
			if (!c->have_media_seq) {
				errlog(c->trk, ".m3u8 parser: expecting #EXT-X-MEDIA-SEQUENCE before the first data file", 0);
				return FMED_RERR;
			}

			ffstr name, ext;
			name = ffm3u_value(&c->m3u);
			ffpath_split3(name.ptr, name.len, NULL, NULL, &ext);
			if (c->first) {
				c->first = 0;
				const fmed_modinfo *mi;
				if (NULL == (mi = core->getmod2(FMED_MOD_INEXT, ext.ptr, ext.len))) {
					errlog(c->trk, "no module configured to open .%S stream", &ext);
					return FMED_RERR;
				}
				if (0 == net->track->cmd(c->trk, FMED_TRACK_FILT_ADD, mi->name))
					return FMED_RERR;
				if (NULL == ffstr_alcopystr(&c->file_ext, &ext))
					return FMED_RSYSERR;
			} else {
				if (!ffstr_eq2(&ext, &c->file_ext)) {
					errlog(c->trk, "stream is changing format from %S to %S"
						, &c->file_ext, &ext);
					return FMED_RERR;
				}
			}

			r = hls_list_add(c, &name, c->m3u_seq++);
			if (r < 0)
				return FMED_RSYSERR;
			break;
		}

		case FFPARS_MORE:
			return FMED_RMORE;

		case FFM3U_EXT: {
			if (c->have_media_seq)
				break;
			//get sequence number from "#EXT-X-MEDIA-SEQUENCE:1234"
			ffstr line, name, val;
			line = ffm3u_value(&c->m3u);
			ffs_split2by(line.ptr, line.len, ':', &name, &val);
			if (!ffstr_eqz(&name, "#EXT-X-MEDIA-SEQUENCE"))
				break;
			uint64 seq;
			if (!ffstr_toint(&val, &seq, FFS_INT64)) {
				errlog(c->trk, "incorrect value: %S", &line);
				return FMED_RERR;
			}
			c->m3u_seq = seq;
			c->have_media_seq = 1;
			break;
		}

		default:
			if (ffpars_iserr(-r)) {
				r = -r;
				errlog(c->trk, ".m3u8 parser: line:%u  (%u) %s"
					, c->m3u.line, r, ffpars_errstr(r));
				return FMED_RERR;
			}
			break;
		}
	}
}



/** HLS client:
. Repeat:
 . Request .m3u8 by HTTP and receive its data
 . Parse the data and get file names
 . Add appropriate filter by file extension (only once)
 . Determine which files are new from the last time (using #EXT-X-MEDIA-SEQUENCE value)
 . If there's no new files, exit
 . Add new files to the queue
 . Get the first file from the queue
 . Prepare a complete URL for the file (base URL for .m3u8 file + file name)
 . Request the file by HTTP
 . Pass file data to the next filters
 . Remove the file from the queue
*/
static int hls_process(void *ctx, fmed_filt *d)
{
	struct hls *c = ctx;
	int r;

	for (;;) {
	switch (c->state) {

	case HLS_GETM3U: {
		if (0 != hls_request(c, c->m3u_url))
			return FMED_RERR;
		c->state = HLS_PARSEM3U;
		return FMED_RASYNC;
	}
		//fallthrough

	case HLS_PARSEM3U:
		r = hls_m3u_parse(c, &c->http_data);
		switch (r) {
		case FMED_RMORE:
			if (c->http_status == FFHTTPCL_DONE) {
				ffm3u_close(&c->m3u);
				ffm3u_init(&c->m3u);
				c->m3u_seq = 0;
				c->have_media_seq = 0;
				http_iface.close(c->con);
				c->con = NULL;
				if (c->qu.len == 0) {
					errlog(c->trk, "no new data files in m3u list", 0);
					return FMED_RERR;
				}
				c->state = HLS_GETTRACK;
				break;
			}
			http_iface.send(c->con, NULL);
			return FMED_RASYNC;
		default:
			c->state = HLS_ERR;
			continue;
		}
		//fallthrough

	case HLS_GETTRACK: {
		ffstr name;
		r = hls_list_pop(c, &name);
		if (r != 0) {
			c->state = HLS_GETM3U;
			continue;
		}
		ffmem_free(c->file_url);
		c->file_url = ffsz_alfmt("%S/%S", &c->base_url, &name);
		ffstr_free(&name);
		if (0 != hls_request(c, c->file_url))
			return FMED_RERR;
		c->state = HLS_FILERESP;
		return FMED_RASYNC;
	}

	case HLS_FILERESP:
		c->state = HLS_FILECONT;
		d->out = c->http_data.ptr,  d->outlen = c->http_data.len;
		return FMED_RDATA;

	case HLS_FILECONT:
		if (c->http_status == FFHTTPCL_DONE) {
			http_iface.close(c->con);
			c->con = NULL;
			c->state = HLS_GETTRACK;
			continue;
		}
		http_iface.send(c->con, NULL);
		c->state = HLS_FILERESP;
		return FMED_RASYNC;

	case HLS_ERR:
		return FMED_RERR;
	}
	}
}

#undef FILT_NAME
