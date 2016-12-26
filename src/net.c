/** IP|TCP|HTTP|ICY input.
Copyright (c) 2016 Simon Zolin */

#include <fmedia.h>

#include <FF/audio/icy.h>
#include <FF/net/url.h>
#include <FF/net/http.h>
#include <FF/data/utf8.h>
#include <FF/list.h>
#include <FFOS/asyncio.h>
#include <FFOS/socket.h>
#include <FFOS/error.h>


typedef struct net_conf {
	uint bufsize;
	uint nbufs;
	uint buf_lowat;
	uint tmout;
	byte user_agent;
	byte max_redirect;
	byte meta;
} net_conf;

typedef struct netmod {
	const fmed_queue *qu;
	const fmed_track *track;
	fflist1 recycled_cons;
	net_conf conf;
} netmod;

static netmod *net;
static const fmed_core *core;

typedef struct icy icy;

typedef struct netin {
	uint state;
	ffarr d[2];
	uint idx;
	fftask task;
	uint fin :1;
	icy *c;
} netin;

struct icy {
	uint state;
	fmed_filt *d;

	fflist1_item recycled;
	char *host;
	ffurl url;
	ffaddrinfo *addr;
	ffaddrinfo *curaddr;
	ffskt sk;
	ffaio_task aio;

	ffstr *bufs;
	uint rbuf;
	uint wbuf;
	size_t curbuf_len;
	uint lowat; //low-watermark number of filled bytes for buffer
	ffstr data;

	ffstr hbuf;
	ffhttp_response resp;
	uint nredirect;

	fficy icy;

	netin *netin;
	ffstr artist;
	ffstr title;

	uint buflock :1
		, iowait :1 //waiting for I/O, all input data is consumed
		, async :1
		, err :1
		, preload :1 //fill all buffers
		;
};

//FMEDIA MODULE
static const void* net_iface(const char *name);
static int net_mod_conf(const char *name, ffpars_ctx *ctx);
static int net_sig(uint signo);
static void net_destroy(void);
static const fmed_mod fmed_net_mod = {
	&net_iface, &net_sig, &net_destroy, &net_mod_conf
};

static int tcp_prepare(icy *c, ffaddr *a);
static void tcp_recv(void *udata);
static void tcp_aio(void *udata);
static void tcp_recvhdrs(void *udata);
static int tcp_getdata(icy *c, ffstr *dst);

static int http_prepreq(icy *c, ffstr *dst);
static int http_parse(icy *c);

//ICY
static void* icy_open(fmed_filt *d);
static int icy_process(void *ctx, fmed_filt *d);
static void icy_close(void *ctx);
static int icy_config(ffpars_ctx *ctx);
static const fmed_filter fmed_icy = {
	&icy_open, &icy_process, &icy_close
};

static int icy_conf_done(ffparser_schem *p, void *obj);

static int icy_setmeta(icy *c, const ffstr *_data);

static void* netin_open(fmed_filt *d);
static int netin_process(void *ctx, fmed_filt *d);
static void netin_close(void *ctx);
static const fmed_filter fmed_netin = {
	&netin_open, &netin_process, &netin_close
};

static void* netin_create(icy *c);
static void netin_write(netin *n, const ffstr *data);


enum {
	UA_OFF,
	UA_NAME,
	UA_NAMEVER,
};

static const char *const http_ua[] = {
	"fmedia", "fmedia/" FMED_VER
};

static const char *const ua_enumstr[] = {
	"off", "name_only", "full",
};
static const ffpars_enumlist ua_enum = { ua_enumstr, FFCNT(ua_enumstr), FFPARS_DSTOFF(net_conf, user_agent) };

static const ffpars_arg net_conf_args[] = {
	{ "bufsize",	FFPARS_TSIZE | FFPARS_FNOTZERO,  FFPARS_DSTOFF(net_conf, bufsize) },
	{ "buffers",	FFPARS_TINT | FFPARS_FNOTZERO,  FFPARS_DSTOFF(net_conf, nbufs) },
	{ "buffer_lowat",	FFPARS_TSIZE,  FFPARS_DSTOFF(net_conf, buf_lowat) },
	{ "timeout",	FFPARS_TINT,  FFPARS_DSTOFF(net_conf, tmout) },
	{ "user_agent",	FFPARS_TENUM | FFPARS_F8BIT,  FFPARS_DST(&ua_enum) },
	{ "max_redirect",	FFPARS_TINT | FFPARS_F8BIT,  FFPARS_DSTOFF(net_conf, max_redirect) },
	{ "meta",	FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(net_conf, meta) },
	{ NULL,	FFPARS_TCLOSE,	FFPARS_DST(&icy_conf_done) },
};


FF_EXP const fmed_mod* fmed_getmod(const fmed_core *_core)
{
	core = _core;
	return &fmed_net_mod;
}


static const void* net_iface(const char *name)
{
	if (!ffsz_cmp(name, "icy"))
		return &fmed_icy;
	else if (!ffsz_cmp(name, "in"))
		return &fmed_netin;
	return NULL;
}

static int net_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "icy"))
		return icy_config(ctx);
	return -1;
}

static int net_sig(uint signo)
{
	switch (signo) {
	case FMED_SIG_INIT:
		ffmem_init();
		if (0 != ffskt_init(FFSKT_SIGPIPE | FFSKT_WSA | FFSKT_WSAFUNCS))
			return -1;
		if (NULL == (net = ffmem_tcalloc1(netmod)))
			return -1;
		return 0;

	case FMED_OPEN:
		if (NULL == (net->qu = core->getmod("#queue.queue")))
			return 1;
		net->track = core->getmod("#core.track");
		break;
	}
	return 0;
}

static void net_destroy(void)
{
	icy *c;
	if (net == NULL)
		return;
	while (NULL != (c = (void*)fflist1_pop(&net->recycled_cons))) {
		ffmem_free(FF_GETPTR(icy, recycled, c));
	}
	ffmem_free0(net);
}


static int icy_config(ffpars_ctx *ctx)
{
	net->conf.bufsize = 16 * 1024;
	net->conf.nbufs = 2;
	net->conf.buf_lowat = 8 * 1024;
	net->conf.tmout = 5000;
	net->conf.user_agent = UA_OFF;
	net->conf.max_redirect = 10;
	net->conf.meta = 1;
	ffpars_setargs(ctx, &net->conf, net_conf_args, FFCNT(net_conf_args));
	return 0;
}

static int icy_conf_done(ffparser_schem *p, void *obj)
{
	net->conf.buf_lowat = ffmin(net->conf.buf_lowat, net->conf.bufsize);
	return 0;
}

static int buf_alloc(icy *c, size_t size)
{
	uint i;
	for (i = 0;  i != net->conf.nbufs;  i++) {
		if (NULL == (ffstr_alloc(&c->bufs[i], size))) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_BUFALOC);
			return -1;
		}
	}
	return 0;
}

static void* icy_open(fmed_filt *d)
{
	icy *c;

	if (NULL != (c = (void*)fflist1_pop(&net->recycled_cons)))
		c = FF_GETPTR(icy, recycled, c);
	else if (NULL == (c = ffmem_tcalloc1(icy)))
		return NULL;
	c->d = d;

	c->host = (void*)d->track->getvalstr(d->trk, "input");
	if (NULL == (c->host = ffsz_alcopyz(c->host)))
		goto done;

	if (NULL == (c->bufs = ffmem_tcalloc(ffstr, net->conf.nbufs))) {
		syserrlog(core, d->trk, NULL, "%e", FFERR_BUFALOC);
		goto done;
	}
	if (0 != buf_alloc(c, net->conf.bufsize))
		goto done;

	return c;

done:
	icy_close(c);
	return NULL;
}

static void icy_close(void *ctx)
{
	icy *c = ctx;

	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	if (c->sk != FF_BADSKT) {
		ffskt_fin(c->sk);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
	}
	ffmem_safefree(c->host);
	ffaio_fin(&c->aio);

	uint i;
	for (i = 0;  i != net->conf.nbufs;  i++) {
		ffstr_free(&c->bufs[i]);
	}
	ffmem_safefree(c->bufs);

	ffstr_free(&c->hbuf);
	ffstr_free(&c->artist);
	ffstr_free(&c->title);

	if (c->netin != NULL) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	uint inst = c->aio.instance;
	ffmem_tzero(c);
	c->aio.instance = inst;
	fflist1_push(&net->recycled_cons, &c->recycled);
}

static int tcp_prepare(icy *c, ffaddr *a)
{
	for (;;) {

		if (c->curaddr == NULL)
			c->curaddr = c->addr;
		else if (c->curaddr->ai_next != NULL)
			c->curaddr = c->curaddr->ai_next;
		else {
			errlog(core, c->d->trk, NULL, "no next address to connect");
			return -1;
		}
		ffaddr_copy(a, c->curaddr->ai_addr, c->curaddr->ai_addrlen);
		ffip_setport(a, c->url.port);

		char saddr[FF_MAXIP6];
		size_t n = ffaddr_tostr(a, saddr, sizeof(saddr), FFADDR_USEPORT);
		ffstr host = ffurl_get(&c->url, c->host, FFURL_HOST);
		core->log(FMED_LOG_INFO, c->d->trk, NULL, "connecting to %S (%*s)...", &host, n, saddr);

		if (FF_BADSKT == (c->sk = ffskt_create(ffaddr_family(a), SOCK_STREAM, IPPROTO_TCP))) {
			core->log(FMED_LOG_WARN | FMED_LOG_SYS, c->d->trk, NULL, "%e", FFERR_SKTCREAT);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
			continue;
		}

		if (0 != ffskt_setopt(c->sk, IPPROTO_TCP, TCP_NODELAY, 1))
			core->log(FMED_LOG_WARN | FMED_LOG_SYS, c->d->trk, NULL, "%e", FFERR_SKTOPT);

		if (0 != ffskt_nblock(c->sk, 1)) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_NBLOCK);
			return -1;
		}
		ffaio_init(&c->aio);
		c->aio.sk = c->sk;
		c->aio.udata = c;
		if (0 != ffaio_attach(&c->aio, core->kq, FFKQU_READ | FFKQU_WRITE)) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_KQUATT);
			return -1;
		}
		return 0;
	}
	//unreachable
}

static void tcp_aio(void *udata)
{
	icy *c = udata;
	c->async = 0;
	c->d->handler(c->d->trk);
}

static void tcp_recvhdrs(void *udata)
{
	icy *c = udata;
	ssize_t r;

	c->async = 0;

	if (c->bufs[0].len == net->conf.bufsize) {
		errlog(core, c->d->trk, "net.icy", "too large response headers");
		c->err = 1;
		goto done;
	}

	r = ffaio_recv(&c->aio, &tcp_recvhdrs, ffarr_end(&c->bufs[0]), net->conf.bufsize - c->bufs[0].len);
	if (r == FFAIO_ASYNC) {
		dbglog(core, c->d->trk, "net.icy", "async recv...");
		c->async = 1;
		return;
	}

	if (r <= 0) {
		if (r == 0)
			errlog(core, c->d->trk, "net.icy", "server has closed connection");
		else
			syserrlog(core, c->d->trk, "net.icy", "%e", FFERR_READ);
		c->err = 1;
		goto done;
	}

	c->bufs[0].len += r;
	dbglog(core, c->d->trk, "net.icy", "recv: +%L [%L]", r, c->bufs[0].len);

done:
	c->d->handler(c->d->trk);
}

static int tcp_getdata(icy *c, ffstr *dst)
{
	if (c->err)
		return FMED_RERR;

	if (c->buflock) {
		c->buflock = 0;
		c->bufs[c->rbuf].len = 0;
		dbglog(core, c->d->trk, NULL, "unlock buf #%u", c->rbuf);
		c->rbuf = (c->rbuf + 1) % net->conf.nbufs;
		if (!c->async)
			tcp_recv(c);
	}

	if (c->bufs[c->rbuf].len == 0) {
		dbglog(core, c->d->trk, NULL, "buf #%u is empty", c->rbuf);
		core->log(FMED_LOG_INFO, c->d->trk, NULL, "precaching data...");
		c->iowait = 1;
		c->preload = 1;
		c->lowat = net->conf.bufsize;
		if (!c->async)
			tcp_recv(c);
		return FMED_RASYNC;
	}

	ffstr_set2(dst, &c->bufs[c->rbuf]);
	c->buflock = 1;
	dbglog(core, c->d->trk, NULL, "lock buf #%u", c->rbuf);
	return FMED_RDATA;
}

static void tcp_recv(void *udata)
{
	icy *c = udata;
	ssize_t r;

	c->async = 0;

	if (c->curbuf_len == net->conf.bufsize) {
		errlog(core, c->d->trk, "net.icy", "buffer #%u is full", c->wbuf);
		c->err = 1;
		goto done;
	}

	r = ffaio_recv(&c->aio, &tcp_recv, c->bufs[c->wbuf].ptr + c->curbuf_len, net->conf.bufsize - c->curbuf_len);
	if (r == FFAIO_ASYNC) {
		dbglog(core, c->d->trk, "net.icy", "buf #%u async recv...", c->wbuf);
		c->async = 1;
		return;
	}

	if (r <= 0) {
		if (r == 0)
			errlog(core, c->d->trk, "net.icy", "server has closed connection");
		else
			syserrlog(core, c->d->trk, "net.icy", "%e", FFERR_READ);
		c->err = 1;
		goto done;
	}

	c->curbuf_len += r;
	dbglog(core, c->d->trk, "net.icy", "buf #%u recv: +%L [%L]", c->wbuf, r, c->curbuf_len);
	if (c->curbuf_len < c->lowat) {
		tcp_recv(c);
		return;
	}

	c->bufs[c->wbuf].len = c->curbuf_len;
	c->curbuf_len = 0;
	c->wbuf = (c->wbuf + 1) % net->conf.nbufs;
	if (c->bufs[c->wbuf].len == 0) {
		// the next buffer is free, so start filling it
		tcp_recv(c);
	}

	if (c->preload) {
		if (c->wbuf != c->rbuf)
			return; //wait until all buffers are filled
		c->preload = 0;
		c->lowat = net->conf.buf_lowat;
	}

done:
	if (c->iowait) {
		dbglog(core, c->d->trk, "net.icy", "waking up track...");
		c->iowait = 0;
		c->d->handler(c->d->trk);
	}
}

enum {
	I_ADDR, I_NEXTADDR, I_CONN,
	I_HTTP_REQ, I_HTTP_REQ_SEND, I_HTTP_RESP, I_HTTP_RESP_PARSE, I_HTTP_RECVBODY,
	I_ICY,
};

static int http_prepreq(icy *c, ffstr *dst)
{
	ffstr s;
	ffhttp_cook ck;

	ffhttp_cookinit(&ck, NULL, 0);
	s = ffurl_get(&c->url, c->host, FFURL_PATHQS);
	if (s.len == 0)
		ffstr_setcz(&s, "/");
	dbglog(core, c->d->trk, NULL, "sending request GET %S", &s);
	ffhttp_addrequest(&ck, "GET", 3, s.ptr, s.len);
	s = ffurl_get(&c->url, c->host, FFURL_FULLHOST);
	ffhttp_addihdr(&ck, FFHTTP_HOST, s.ptr, s.len);
	if (net->conf.user_agent != 0) {
		ffstr_setz(&s, http_ua[net->conf.user_agent - 1]);
		ffhttp_addihdr(&ck, FFHTTP_USERAGENT, s.ptr, s.len);
	}
	if (net->conf.meta) {
		ffstr_setz(&s, "1");
		ffhttp_addhdr_str(&ck, &fficy_shdr[FFICY_HMETADATA], &s);
	}
	ffhttp_cookfin(&ck);
	ffstr_acqstr3(&c->hbuf, &ck.buf);
	ffstr_set2(dst, &c->hbuf);
	ffhttp_cookdestroy(&ck);
	return 0;
}

/**
Return 0 on success;  1 if a new state is set;  -1 on error. */
static int http_parse(icy *c)
{
	ffstr s;
	int r = ffhttp_respparse_all(&c->resp, c->bufs[0].ptr, c->bufs[0].len, FFHTTP_IGN_STATUS_PROTO);
	switch (r) {
	case FFHTTP_DONE:
		break;
	case FFHTTP_MORE:
		c->state = I_HTTP_RESP;
		return 1;
	default:
		errlog(core, c->d->trk, NULL, "parse HTTP response: %s", ffhttp_errstr(r));
		return -1;
	}

	dbglog(core, c->d->trk, NULL, "HTTP response: %*s", c->resp.h.len, c->bufs[0].ptr);

	if ((c->resp.code == 301 || c->resp.code == 302)
		&& c->nredirect++ != net->conf.max_redirect
		&& 0 != ffhttp_findhdr(&c->resp.h, ffhttp_shdr[FFHTTP_LOCATION].ptr, ffhttp_shdr[FFHTTP_LOCATION].len, &s)) {

		dbglog(core, c->d->trk, NULL, "HTTP redirect: %S", &s);
		if (c->sk != FF_BADSKT) {
			ffskt_fin(c->sk);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
		}
		ffaio_fin(&c->aio);
		ffmem_free(c->host);
		if (NULL == (c->host = ffsz_alcopy(s.ptr, s.len))) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_BUFALOC);
			return -1;
		}
		ffurl_init(&c->url);
		c->bufs[0].len = 0;
		c->state = I_ADDR;
		return 1;

	} else if (c->resp.code != 200) {
		errlog(core, c->d->trk, NULL, "bad HTTP response code");
		return -1;
	}

	return 0;
}

static int icy_process(void *ctx, fmed_filt *d)
{
	icy *c = ctx;
	ssize_t r;
	ffstr s;
	ffaddr a = {0};
	char *hostz;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	for (;;) {
	switch (c->state) {
	case I_ADDR:
		if (0 != ffurl_parse(&c->url, c->host, ffsz_len(c->host))) {
			errlog(core, c->d->trk, NULL, "ffurl_parse");
			goto done;
		}
		if (c->url.port == 0)
			c->url.port = FFHTTP_PORT;

		s = ffurl_get(&c->url, c->host, FFURL_HOST);
		if (NULL == (hostz = ffsz_alcopy(s.ptr, s.len))) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_BUFALOC);
			goto done;
		}

		core->log(FMED_LOG_INFO, c->d->trk, NULL, "resolving host %S...", &s);
		r = ffaddr_info(&c->addr, hostz, NULL, 0);
		ffmem_free(hostz);
		if (r != 0) {
			syserrlog(core, c->d->trk, NULL, "%e", FFERR_RESOLVE);
			goto done;
		}
		c->state = I_NEXTADDR;
		// break

	case I_NEXTADDR:
		if (0 != tcp_prepare(c, &a))
			goto done;
		c->state = I_CONN;
		// break

	case I_CONN:
		r = ffaio_connect(&c->aio, &tcp_aio, &a.a, a.len);
		if (r == FFAIO_ERROR) {
			core->log(FMED_LOG_WARN | FMED_LOG_SYS, c->d->trk, NULL, "%e", FFERR_SKTCONN);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
			ffaio_fin(&c->aio);
			c->state = I_NEXTADDR;
			continue;

		} else if (r == FFAIO_ASYNC) {
			c->async = 1;
			return FMED_RASYNC;
		}
		ffaddr_free(c->addr);
		c->addr = NULL;
		c->curaddr = NULL;
		c->state = I_HTTP_REQ;
		// break

	case I_HTTP_REQ:
		http_prepreq(c, &c->data);
		c->state = I_HTTP_REQ_SEND;
		// break

	case I_HTTP_REQ_SEND:
		r = ffskt_send(c->sk, c->data.ptr, c->data.len, 0);
		if (r < 0) {
			syserrlog(core, c->d->trk, NULL, "%s", "ffskt_send");
			goto done;
		}
		ffstr_shift(&c->data, r);
		if (c->data.len != 0)
			continue;

		dbglog(core, c->d->trk, NULL, "receiving response...");
		ffhttp_respinit(&c->resp);
		ffstr_set(&c->data, c->bufs[0].ptr, net->conf.bufsize);
		c->state = I_HTTP_RESP;
		// break

	case I_HTTP_RESP:
		c->state = I_HTTP_RESP_PARSE;
		tcp_recvhdrs(c);
		return FMED_RASYNC;

	case I_HTTP_RESP_PARSE:
		if (c->err)
			goto done;
		r = http_parse(c);
		if (r == 1)
			continue;
		else if (r == -1)
			goto done;

		{
		uint meta_int = FFICY_NOMETA;
		if (0 != ffhttp_findhdr(&c->resp.h, fficy_shdr[FFICY_HMETAINT].ptr, fficy_shdr[FFICY_HMETAINT].len, &s)) {
			ffs_toint(s.ptr, s.len, &meta_int, FFS_INT32);
		}
		fficy_parseinit(&c->icy, meta_int);
		}

		ffstr_set2(&c->data, &c->bufs[0]);
		ffstr_shift(&c->data, c->resp.h.len);
		c->bufs[0].len = 0;
		c->state = I_ICY;
		continue;
		// break

	case I_HTTP_RECVBODY:
		r = tcp_getdata(c, &c->data);
		if (r == FMED_RASYNC)
			return FMED_RASYNC;
		else if (r == FMED_RERR)
			goto done;
		c->state = I_ICY;
		// break

	case I_ICY:
		if (c->data.len == 0) {
			c->state = I_HTTP_RECVBODY;
			continue;
		}
		{
		size_t n = c->data.len;
		r = fficy_parse(&c->icy, c->data.ptr, &n, &s);
		ffstr_shift(&c->data, n);
		}
		switch (r) {
		case FFICY_RDATA:
			if (c->netin != NULL) {
				netin_write(c->netin, &s);
			}

			d->out = s.ptr;
			d->outlen = s.len;
			return FMED_RDATA;

		// case FFICY_RMETACHUNK:

		case FFICY_RMETA:
			icy_setmeta(c, &s);
			break;
		}
	}
	}

	//unreachable

done:
	return FMED_RERR;
}

static int icy_setmeta(icy *c, const ffstr *_data)
{
	fficymeta icymeta;
	ffstr artist = {0}, title = {0}, data = *_data, pair[2];
	fmed_que_entry *qent;
	int r;
	ffbool istitle = 0;

	dbglog(core, c->d->trk, NULL, "meta: [%L] %S", data.len, &data);
	fficy_metaparse_init(&icymeta);

	while (data.len != 0) {
		size_t n = data.len;
		r = fficy_metaparse(&icymeta, data.ptr, &n);
		ffstr_shift(&data, n);

		switch (r) {
		case FFPARS_KEY:
			if (ffstr_eqcz(&icymeta.val, "StreamTitle"))
				istitle = 1;
			break;

		case FFPARS_VAL:
			if (istitle) {
				fficy_streamtitle(icymeta.val.ptr, icymeta.val.len, &artist, &title);
				data.len = 0;
			}
			break;

		default:
			errlog(core, c->d, NULL, "bad metadata");
			data.len = 0;
			break;
		}
	}

	ffstr_free(&c->artist);
	ffstr_free(&c->title);

	qent = (void*)c->d->track->getval(c->d->trk, "queue_item");
	ffstr_setcz(&pair[0], "artist");
	ffarr utf = {0};
	ffutf8_strencode(&utf, artist.ptr, artist.len, FFU_WIN1252);
	ffstr_set2(&pair[1], &utf);
	net->qu->cmd2(FMED_QUE_METASET | (FMED_QUE_OVWRITE << 16), qent, (size_t)pair);
	ffstr_acqstr3(&c->artist, &utf);

	ffstr_setcz(&pair[0], "title");
	ffutf8_strencode(&utf, title.ptr, title.len, FFU_WIN1252);
	ffstr_set2(&pair[1], &utf);
	net->qu->cmd2(FMED_QUE_METASET | (FMED_QUE_OVWRITE << 16), qent, (size_t)pair);
	c->d->meta_changed = 1;
	ffstr_acqstr3(&c->title, &utf);

	if (c->netin != NULL) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	if (c->netin == NULL && FMED_NULL != net->track->getval(c->d->trk, "out-copy"))
		c->netin = netin_create(c);
	return 0;
}


enum { IN_WAIT = 1, IN_DATANEXT };

static void* netin_create(icy *c)
{
	netin *n;
	void *trk;
	if (NULL == (n = ffmem_tcalloc1(netin)))
		return NULL;

	if (NULL == (trk = net->track->create(FMED_TRACK_NET, "")))
		return NULL;

	const char *output;
	output = net->track->getvalstr(c->d->trk, "output");
	net->track->setvalstr(trk, "output", output);

	net->track->setvalstr(trk, "input", "?.mp3");
	net->track->setval(trk, "netin_ptr", (size_t)n);
	n->c = c;

	if (1 == net->track->getval(c->d->trk, "stream_copy"))
		net->track->setval(trk, "stream_copy", 1);

	net->track->setvalstr4(trk, "artist", (void*)&c->artist, FMED_TRK_META | FMED_TRK_VALSTR);
	net->track->setvalstr4(trk, "title", (void*)&c->title, FMED_TRK_META | FMED_TRK_VALSTR);

	net->track->cmd(trk, FMED_TRACK_START);
	return n;
}

static void netin_write(netin *n, const ffstr *data)
{
	if (data == NULL)
		n->fin = 1;
	else
		ffarr_append(&n->d[n->idx], data->ptr, data->len);
	if (n->state == IN_WAIT)
		core->task(&n->task, FMED_TASK_POST);
}

static void* netin_open(fmed_filt *d)
{
	netin *n;
	n = (void*)fmed_getval("netin_ptr");
	n->state = IN_DATANEXT;
	n->task.param = d->trk;
	n->task.handler = d->handler;
	return n;
}

static void netin_close(void *ctx)
{
	netin *n = ctx;
	ffarr_free(&n->d[0]);
	ffarr_free(&n->d[1]);
	core->task(&n->task, FMED_TASK_DEL);
	n->c->netin = NULL;
	ffmem_free(n);
}

static int netin_process(void *ctx, fmed_filt *d)
{
	netin *n = ctx;
	switch (n->state) {
	case IN_DATANEXT:
		if (n->d[n->idx].len == 0 && !n->fin) {
			n->state = IN_WAIT;
			return FMED_RASYNC;
		}
		break;

	case IN_WAIT:
		break;
	}
	n->state = IN_DATANEXT;
	d->out = n->d[n->idx].ptr,  d->outlen = n->d[n->idx].len;
	n->d[n->idx].len = 0;
	n->idx = (n->idx + 1) % 2;
	if (n->fin)
		return FMED_RDONE;
	return FMED_RDATA;
}
