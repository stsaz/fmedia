/** IP|TCP|HTTP input;  ICY input.
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


#undef dbglog
#undef warnlog
#undef errlog
#undef syserrlog
#define dbglog(trk, ...)  fmed_dbglog(core, trk, "net.icy", __VA_ARGS__)
#define infolog(trk, ...)  fmed_infolog(core, trk, "net.icy", __VA_ARGS__)
#define warnlog(trk, ...)  fmed_warnlog(core, trk, "net.icy", __VA_ARGS__)
#define syswarnlog(trk, ...)  fmed_syswarnlog(core, trk, "net.icy", __VA_ARGS__)
#define errlog(trk, ...)  fmed_errlog(core, trk, "net.icy", __VA_ARGS__)
#define syserrlog(trk, ...)  fmed_syserrlog(core, trk, "net.icy", __VA_ARGS__)


typedef struct net_conf {
	uint bufsize;
	uint nbufs;
	uint buf_lowat;
	uint tmout;
	byte user_agent;
	byte max_redirect;
	byte max_reconnect;
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
	uint fn_dyn :1;
	icy *c;
} netin;

typedef struct nethttp {
	uint state;
	fmed_filt *d;

	const char *method;
	fflist1_item recycled;
	char *orighost;
	char *host;
	ffurl url;
	ffiplist iplist;
	ffip6 ip;
	ffaddrinfo *addr;
	ffip_iter curaddr;
	ffskt sk;
	ffaio_task aio;
	uint reconnects;
	fftmrq_entry tmr;

	ffstr *bufs;
	uint rbuf;
	uint wbuf;
	size_t curbuf_len;
	uint lowat; //low-watermark number of filled bytes for buffer
	ffstr data;

	ffstr hbuf;
	ffhttp_response resp;
	uint nredirect;
	uint max_reconnect;

	uint buflock :1
		, iowait :1 //waiting for I/O, all input data is consumed
		, async :1
		, preload :1 //fill all buffers
		, icy_meta_req :1
		;
} nethttp;

struct icy {
	fmed_filt *d;
	fficy icy;
	ffstr data;
	ffstr next_filt_ext;

	netin *netin;
	ffstr artist;
	ffstr title;

	uint out_copy :1;
	uint save_oncmd :1;
};

//FMEDIA MODULE
static const void* net_iface(const char *name);
static int net_mod_conf(const char *name, ffpars_ctx *ctx);
static int net_sig(uint signo);
static void net_destroy(void);
static const fmed_mod fmed_net_mod = {
	.ver = FMED_VER_FULL, .ver_core = FMED_VER_CORE,
	&net_iface, &net_sig, &net_destroy, &net_mod_conf
};

static int ip_resolve(nethttp *c);
static int tcp_prepare(nethttp *c, ffaddr *a);
static int tcp_connect(nethttp *c, const struct sockaddr *addr, socklen_t addr_size);
static int tcp_recv(nethttp *c);
static void tcp_ontmr(void *param);
static int tcp_recvhdrs(nethttp *c);
static int tcp_send(nethttp *c);
static int tcp_getdata(nethttp *c, ffstr *dst);
static int tcp_ioerr(nethttp *c);

//HTTP
static int http_config(ffpars_ctx *ctx);
static void* http_open(fmed_filt *d);
static int http_process(void *ctx, fmed_filt *d);
static void http_close(void *ctx);
static const fmed_filter fmed_http = {
	&http_open, &http_process, &http_close
};

static int http_conf_done(ffparser_schem *p, void *obj);
static int http_prepreq(nethttp *c, ffstr *dst);
static int http_parse(nethttp *c);

//ICY
static void* icy_open(fmed_filt *d);
static int icy_process(void *ctx, fmed_filt *d);
static void icy_close(void *ctx);
static int icy_config(ffpars_ctx *ctx);
static const fmed_filter fmed_icy = {
	&icy_open, &icy_process, &icy_close
};

static int icy_reset(icy *c, fmed_filt *d);
static int icy_setmeta(icy *c, const ffstr *_data);

//PASS-THROUGH
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
	{ "max_reconnect",	FFPARS_TINT8,  FFPARS_DSTOFF(net_conf, max_reconnect) },
	{ NULL,	FFPARS_TCLOSE,	FFPARS_DST(&http_conf_done) },
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
	else if (!ffsz_cmp(name, "http"))
		return &fmed_http;
	return NULL;
}

static int net_mod_conf(const char *name, ffpars_ctx *ctx)
{
	if (!ffsz_cmp(name, "icy"))
		return icy_config(ctx);
	else if (!ffsz_cmp(name, "http"))
		return http_config(ctx);
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
		ffhttp_initheaders();
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
	nethttp *c;
	if (net == NULL)
		return;
	while (NULL != (c = (void*)fflist1_pop(&net->recycled_cons))) {
		ffmem_free(FF_GETPTR(nethttp, recycled, c));
	}
	ffmem_free0(net);
	ffhttp_freeheaders();
}


static int http_config(ffpars_ctx *ctx)
{
	net->conf.bufsize = 16 * 1024;
	net->conf.nbufs = 2;
	net->conf.buf_lowat = 8 * 1024;
	net->conf.tmout = 5000;
	net->conf.user_agent = UA_OFF;
	net->conf.max_redirect = 10;
	net->conf.max_reconnect = 3;
	ffpars_setargs(ctx, &net->conf, net_conf_args, FFCNT(net_conf_args));
	return 0;
}

static int http_conf_done(ffparser_schem *p, void *obj)
{
	net->conf.buf_lowat = ffmin(net->conf.buf_lowat, net->conf.bufsize);
	return 0;
}

static int buf_alloc(nethttp *c, size_t size)
{
	uint i;
	for (i = 0;  i != net->conf.nbufs;  i++) {
		if (NULL == (ffstr_alloc(&c->bufs[i], size))) {
			syserrlog(c->d->trk, "%s", ffmem_alloc_S);
			return -1;
		}
	}
	return 0;
}

enum {
	I_ADDR, I_NEXTADDR, I_CONN,
	I_HTTP_REQ, I_HTTP_REQ_SEND, I_HTTP_RESP, I_HTTP_RESP_PARSE, I_HTTP_RECVBODY1, I_HTTP_RECVBODY,
	I_DONE, I_ERR,
};

static void* http_open(fmed_filt *d)
{
	nethttp *c;

	if (NULL != (c = (void*)fflist1_pop(&net->recycled_cons)))
		c = FF_GETPTR(nethttp, recycled, c);
	else if (NULL == (c = ffmem_new(nethttp)))
		return NULL;
	c->d = d;
	c->sk = FF_BADSKT;
	ffhttp_respinit(&c->resp);

	c->host = (void*)d->track->getvalstr(d->trk, "input");
	if (NULL == (c->host = ffsz_alcopyz(c->host)))
		goto done;
	c->orighost = c->host;

	if (NULL == (c->bufs = ffmem_callocT(net->conf.nbufs, ffstr))) {
		syserrlog(d->trk, "%s", ffmem_alloc_S);
		goto done;
	}
	if (0 != buf_alloc(c, net->conf.bufsize))
		goto done;

	c->tmr.handler = &tcp_ontmr;
	c->tmr.param = c;
	c->method = "GET";
	c->max_reconnect = net->conf.max_reconnect;
	c->icy_meta_req = net->conf.meta;

	return c;

done:
	http_close(c);
	return NULL;
}

static void http_close(void *ctx)
{
	nethttp *c = ctx;

	core->timer(&c->tmr, 0, 0);
	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	if (c->sk != FF_BADSKT) {
		ffskt_fin(c->sk);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
	}
	if (c->host != c->orighost)
		ffmem_safefree(c->host);
	ffmem_safefree(c->orighost);
	ffaio_fin(&c->aio);
	ffhttp_respfree(&c->resp);

	uint i;
	for (i = 0;  i != net->conf.nbufs;  i++) {
		ffstr_free(&c->bufs[i]);
	}
	ffmem_safefree(c->bufs);

	ffstr_free(&c->hbuf);

	uint inst = c->aio.instance;
	ffmem_tzero(c);
	c->aio.instance = inst;
	fflist1_push(&net->recycled_cons, &c->recycled);
}

static int ip_resolve(nethttp *c)
{
	ffstr s;
	char *hostz;
	int r;

	if (0 != ffurl_parse(&c->url, c->host, ffsz_len(c->host))) {
		errlog(c->d->trk, "ffurl_parse");
		goto done;
	}
	if (c->url.port == 0)
		c->url.port = FFHTTP_PORT;

	r = ffurl_parse_ip(&c->url, c->host, &c->ip);
	if (r < 0) {
		s = ffurl_get(&c->url, c->host, FFURL_HOST);
		errlog(c->d->trk, "bad IP address: %S", &s);
		goto done;
	} else if (r != 0) {
		ffip_list_set(&c->iplist, r, &c->ip);
		ffip_iter_set(&c->curaddr, &c->iplist, NULL);
		return 0;
	}

	s = ffurl_get(&c->url, c->host, FFURL_HOST);
	if (NULL == (hostz = ffsz_alcopy(s.ptr, s.len))) {
		syserrlog(c->d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	infolog(c->d->trk, "resolving host %S...", &s);
	r = ffaddr_info(&c->addr, hostz, NULL, 0);
	ffmem_free(hostz);
	if (r != 0) {
		syserrlog(c->d->trk, "%s", ffaddr_info_S);
		goto done;
	}
	ffip_iter_set(&c->curaddr, NULL, c->addr);

	if (core->loglev == FMED_LOG_DEBUG) {
		size_t n;
		char buf[FF_MAXIP6];
		ffip_iter it;
		ffip_iter_set(&it, NULL, c->addr);
		uint fam;
		void *ip;
		while (0 != (fam = ffip_next(&it, &ip))) {
			n = ffip_tostr(buf, sizeof(buf), fam, ip, 0);
			dbglog(c->d->trk, "%*s", n, buf);
		}
	}

	return 0;

done:
	return -1;
}

static int tcp_prepare(nethttp *c, ffaddr *a)
{
	void *ip;
	int family;
	while (0 != (family = ffip_next(&c->curaddr, &ip))) {

		ffaddr_setip(a, family, ip);
		ffip_setport(a, c->url.port);

		char saddr[FF_MAXIP6];
		size_t n = ffaddr_tostr(a, saddr, sizeof(saddr), FFADDR_USEPORT);
		ffstr host = ffurl_get(&c->url, c->host, FFURL_HOST);
		infolog(c->d->trk, "connecting to %S (%*s)...", &host, n, saddr);

		if (FF_BADSKT == (c->sk = ffskt_create(family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP))) {
			syswarnlog(c->d->trk, "%s", ffskt_create_S);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
			continue;
		}

		if (0 != ffskt_setopt(c->sk, IPPROTO_TCP, TCP_NODELAY, 1))
			syswarnlog(c->d->trk, "%s", ffskt_setopt_S);

		ffaio_init(&c->aio);
		c->aio.sk = c->sk;
		c->aio.udata = c;
		if (0 != ffaio_attach(&c->aio, core->kq, FFKQU_READ | FFKQU_WRITE)) {
			syserrlog(c->d->trk, "%s", ffkqu_attach_S);
			return -1;
		}
		return 0;
	}

	errlog(c->d->trk, "no next address to connect");
	return -1;
}

static void tcp_aio(void *udata)
{
	nethttp *c = udata;
	c->async = 0;
	core->timer(&c->tmr, 0, 0);
	c->d->handler(c->d->trk);
}

static int tcp_connect(nethttp *c, const struct sockaddr *addr, socklen_t addr_size)
{
	int r;
	r = ffaio_connect(&c->aio, &tcp_aio, addr, addr_size);
	if (r == FFAIO_ERROR) {
		syswarnlog(c->d->trk, "%s", ffskt_connect_S);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
		ffaio_fin(&c->aio);
		return FMED_RMORE;

	} else if (r == FFAIO_ASYNC) {
		c->async = 1;
		core->timer(&c->tmr, -(int)net->conf.tmout, 0);
		return FMED_RASYNC;
	}

	dbglog(c->d->trk, "%s ok", ffskt_connect_S);
	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	ffmem_tzero(&c->curaddr);
	return 0;
}

static void tcp_ontmr(void *param)
{
	nethttp *c = param;
	warnlog(c->d->trk, "I/O timeout", 0);
	c->async = 0;
	tcp_ioerr(c);
	c->d->handler(c->d->trk);
}

static int tcp_recvhdrs(nethttp *c)
{
	ssize_t r;

	if (c->bufs[0].len == net->conf.bufsize) {
		errlog(c->d->trk, "too large response headers");
		return FMED_RMORE;
	}

	r = ffaio_recv(&c->aio, &tcp_aio, ffarr_end(&c->bufs[0]), net->conf.bufsize - c->bufs[0].len);
	if (r == FFAIO_ASYNC) {
		dbglog(c->d->trk, "async recv...");
		c->async = 1;
		core->timer(&c->tmr, -(int)net->conf.tmout, 0);
		return FMED_RASYNC;
	}

	if (r <= 0) {
		if (r == 0)
			errlog(c->d->trk, "server has closed connection");
		else
			syserrlog(c->d->trk, "%s", ffskt_recv_S);
		return FMED_RERR;
	}

	c->bufs[0].len += r;
	dbglog(c->d->trk, "recv: +%L [%L]", r, c->bufs[0].len);
	return 0;
}

static int tcp_getdata(nethttp *c, ffstr *dst)
{
	int r;
	if (c->buflock) {
		c->buflock = 0;
		c->bufs[c->rbuf].len = 0;
		dbglog(c->d->trk, "unlock buf #%u", c->rbuf);
		c->rbuf = ffint_cycleinc(c->rbuf, net->conf.nbufs);
	}

	if (c->bufs[c->rbuf].len == 0) {
		FF_ASSERT(!c->async);
		r = tcp_recv(c);
		if (r == FMED_RASYNC) {
			infolog(c->d->trk, "precaching data...");
			c->iowait = 1;
			c->preload = 1;
			c->lowat = net->conf.bufsize;
			return FMED_RASYNC;
		} else if (r == FMED_RERR) {
			tcp_ioerr(c);
			return FMED_RMORE;
		} else if (r == FMED_RDONE) {
			c->state = I_DONE;
			return FMED_RMORE;
		} else if (r == FMED_RMORE) {
			c->state = I_ERR;
			return FMED_RMORE;
		}
	}

	if (c->preload) {
		c->preload = 0;
		c->lowat = net->conf.buf_lowat;
	}

	ffstr_set2(dst, &c->bufs[c->rbuf]);
	c->buflock = 1;
	dbglog(c->d->trk, "lock buf #%u", c->rbuf);
	return FMED_RDATA;
}

static int tcp_ioerr(nethttp *c)
{
	if (c->reconnects++ == c->max_reconnect) {
		errlog(c->d->trk, "reached max number of reconnections", 0);
		c->state = I_ERR;
		return 1;
	}

	ffskt_fin(c->sk);
	ffskt_close(c->sk);
	c->sk = FF_BADSKT;
	ffaio_fin(&c->aio);

	if (c->host != c->orighost) {
		ffmem_free(c->host);
		c->host = c->orighost;
	}
	ffmem_tzero(&c->url);
	ffmem_tzero(&c->iplist);
	ffmem_tzero(&c->ip);

	c->bufs[0].len = 0;
	c->curbuf_len = 0;
	c->wbuf = c->rbuf = 0;
	c->lowat = 0;

	ffhttp_respfree(&c->resp);
	ffhttp_respinit(&c->resp);
	c->state = I_ADDR;
	dbglog(c->d->trk, "reconnecting...", 0);
	return 0;
}

static void tcp_recv_a(void *udata)
{
	nethttp *c = udata;
	core->timer(&c->tmr, 0, 0);
	c->async = 0;
	int r = tcp_recv(c);
	if (r == FMED_RASYNC)
		return;
	else if (r == FMED_RERR)
		tcp_ioerr(c);
	else if (r == FMED_RDONE)
		c->state = I_DONE;
	else if (r == FMED_RMORE)
		c->state = I_ERR;
	if (c->iowait) {
		dbglog(c->d->trk, "waking up track...");
		c->iowait = 0;
		c->d->handler(c->d->trk);
	}
}

static int tcp_recv(nethttp *c)
{
	ssize_t r;

	if (c->curbuf_len == net->conf.bufsize) {
		errlog(c->d->trk, "buffer #%u is full", c->wbuf);
		return FMED_RMORE;
	}

	for (;;) {

		dbglog(c->d->trk, "buf #%u recv...  rpending:%u  size:%u"
			, c->wbuf, c->aio.rpending
			, (int)net->conf.bufsize - (int)c->curbuf_len);
		r = ffaio_recv(&c->aio, &tcp_recv_a, c->bufs[c->wbuf].ptr + c->curbuf_len, net->conf.bufsize - c->curbuf_len);
		if (r == FFAIO_ASYNC) {
			dbglog(c->d->trk, "buf #%u async recv...", c->wbuf);
			c->async = 1;
			core->timer(&c->tmr, -(int)net->conf.tmout, 0);
			return FMED_RASYNC;
		}

		if (r == 0) {
			dbglog(c->d->trk, "server has closed connection");
			return FMED_RDONE;
		} else if (r < 0) {
			syserrlog(c->d->trk, "%s", ffskt_recv_S);
			return FMED_RERR;
		}

		c->curbuf_len += r;
		dbglog(c->d->trk, "buf #%u recv: +%L [%L]", c->wbuf, r, c->curbuf_len);
		if (c->curbuf_len < c->lowat)
			continue;

		c->bufs[c->wbuf].len = c->curbuf_len;
		c->curbuf_len = 0;
		c->wbuf = ffint_cycleinc(c->wbuf, net->conf.nbufs);
		if (c->preload && c->bufs[c->wbuf].len == 0) {
			// the next buffer is free, so start filling it
			continue;
		}

		break;
	}

	return FMED_RDATA;
}

static int tcp_send(nethttp *c)
{
	int r;

	for (;;) {
		r = ffaio_send(&c->aio, &tcp_aio, c->data.ptr, c->data.len);
		if (r == FFAIO_ERROR) {
			syserrlog(c->d->trk, "%s", ffskt_send_S);
			return FMED_RERR;

		} else if (r == FFAIO_ASYNC) {
			c->async = 1;
			core->timer(&c->tmr, -(int)net->conf.tmout, 0);
			return FMED_RASYNC;
		}

		dbglog(c->d->trk, "send: +%u", r);
		ffstr_shift(&c->data, r);
		if (c->data.len == 0)
			return 0;
	}
}

static int http_prepreq(nethttp *c, ffstr *dst)
{
	ffstr s;
	ffhttp_cook ck;

	ffhttp_cookinit(&ck, NULL, 0);
	s = ffurl_get(&c->url, c->host, FFURL_PATHQS);
	if (s.len == 0)
		ffstr_setcz(&s, "/");
	dbglog(c->d->trk, "sending request %s %S", c->method, &s);
	ffhttp_addrequest(&ck, c->method, ffsz_len(c->method), s.ptr, s.len);
	s = ffurl_get(&c->url, c->host, FFURL_FULLHOST);
	ffhttp_addihdr(&ck, FFHTTP_HOST, s.ptr, s.len);
	if (net->conf.user_agent != 0) {
		ffstr_setz(&s, http_ua[net->conf.user_agent - 1]);
		ffhttp_addihdr(&ck, FFHTTP_USERAGENT, s.ptr, s.len);
	}
	if (c->icy_meta_req) {
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
Return 0 on success;  1 - need more data;  2 - redirect;  -1 on error;  -2 if non-200 response. */
static int http_parse(nethttp *c)
{
	ffstr s;
	int r = ffhttp_respparse_all(&c->resp, c->bufs[0].ptr, c->bufs[0].len, FFHTTP_IGN_STATUS_PROTO);
	switch (r) {
	case FFHTTP_DONE:
		break;
	case FFHTTP_MORE:
		return 1;
	default:
		errlog(c->d->trk, "parse HTTP response: %s", ffhttp_errstr(r));
		return -1;
	}

	dbglog(c->d->trk, "HTTP response: %*s", c->resp.h.len, c->bufs[0].ptr);

	if ((c->resp.code == 301 || c->resp.code == 302)
		&& c->nredirect++ != net->conf.max_redirect
		&& 0 != ffhttp_findihdr(&c->resp.h, FFHTTP_LOCATION, &s)) {

		infolog(c->d->trk, "HTTP redirect: %S", &s);
		if (c->sk != FF_BADSKT) {
			ffskt_fin(c->sk);
			ffskt_close(c->sk);
			c->sk = FF_BADSKT;
		}
		ffaio_fin(&c->aio);
		if (c->host != c->orighost)
			ffmem_free(c->host);
		if (NULL == (c->host = ffsz_alcopy(s.ptr, s.len))) {
			syserrlog(c->d->trk, "%s", ffmem_alloc_S);
			return -1;
		}
		ffurl_init(&c->url);
		c->bufs[0].len = 0;
		ffhttp_respfree(&c->resp);
		ffhttp_respinit(&c->resp);
		return 2;

	} else if (c->resp.code != 200) {
		ffstr ln = ffhttp_respstatus(&c->resp);
		errlog(c->d->trk, "resource unavailable: %S", &ln);
		return -2;
	}

	return 0;
}

static int http_process(void *ctx, fmed_filt *d)
{
	nethttp *c = ctx;
	ssize_t r;
	ffaddr a = {0};

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RLASTOUT;
	}

	for (;;) {
	switch (c->state) {
	case I_ADDR:
		if (0 != ip_resolve(c))
			goto done;
		c->state = I_NEXTADDR;
		// break

	case I_NEXTADDR:
		if (0 != tcp_prepare(c, &a))
			goto done;
		c->state = I_CONN;
		// break

	case I_CONN:
		r = tcp_connect(c, &a.a, a.len);
		if (r == FMED_RASYNC)
			return FMED_RASYNC;
		else if (r == FMED_RMORE) {
			c->state = I_NEXTADDR;
			continue;
		}
		c->state = I_HTTP_REQ;
		// break

	case I_HTTP_REQ:
		http_prepreq(c, &c->data);
		c->state = I_HTTP_REQ_SEND;
		// break

	case I_HTTP_REQ_SEND:
		r = tcp_send(c);
		if (r == FMED_RASYNC)
			return FMED_RASYNC;
		else if (r == FMED_RERR) {
			tcp_ioerr(c);
			continue;
		}

		dbglog(c->d->trk, "receiving response...");
		ffstr_set(&c->data, c->bufs[0].ptr, net->conf.bufsize);
		c->state = I_HTTP_RESP;
		// break

	case I_HTTP_RESP:
		r = tcp_recvhdrs(c);
		if (r == FMED_RASYNC)
			return FMED_RASYNC;
		else if (r == FMED_RMORE) {
			c->state = I_ERR;
			continue;
		} else if (r == FMED_RERR) {
			tcp_ioerr(c);
			continue;
		}
		c->state = I_HTTP_RESP_PARSE;
		//fall through

	case I_HTTP_RESP_PARSE:
		r = http_parse(c);
		switch (r) {
		case 0:
			break;
		case -1:
		case -2:
			goto done;
		case 1:
			c->state = I_HTTP_RESP;
			continue;
		case 2:
			c->state = I_ADDR;
			continue;
		}

		ffstr_set2(&c->data, &c->bufs[0]);
		ffstr_shift(&c->data, c->resp.h.len);
		c->bufs[0].len = 0;
		d->net_reconnect = 1;
		d->out = c->data.ptr,  d->outlen = c->data.len;
		c->state = I_HTTP_RECVBODY1;
		return FMED_RDATA;

	case I_HTTP_RECVBODY1:
		ffhttp_respfree(&c->resp);
		c->state = I_HTTP_RECVBODY;
		//fall through

	case I_HTTP_RECVBODY:
		r = tcp_getdata(c, &c->data);
		if (r == FMED_RASYNC)
			return FMED_RASYNC;
		else if (r == FMED_RERR)
			goto done;
		else if (r == FMED_RMORE)
			continue;
		d->out = c->data.ptr,  d->outlen = c->data.len;
		return FMED_RDATA;

	case I_DONE:
		d->outlen = 0;
		return FMED_RDONE;

	case I_ERR:
		return FMED_RERR;
	}
	}

	//unreachable

done:
	return FMED_RERR;
}

/** Find HTTP response header. */
static int http_findhdr(nethttp *c, const ffstr *name, ffstr *dst)
{
	return ffhttp_findhdr(&c->resp.h, name->ptr, name->len, dst);
}


static const ffpars_arg icy_conf_args[] = {
	{ "meta",	FFPARS_TBOOL | FFPARS_F8BIT,  FFPARS_DSTOFF(net_conf, meta) },
};

static int icy_config(ffpars_ctx *ctx)
{
	net->conf.meta = 1;
	ffpars_setargs(ctx, &net->conf, icy_conf_args, FFCNT(icy_conf_args));
	return 0;
}

static void* icy_open(fmed_filt *d)
{
	icy *c;
	if (NULL == (c = ffmem_new(icy)))
		return NULL;
	c->d = d;

	if (0 != net->track->cmd2(d->trk, FMED_TRACK_ADDFILT_PREV, "net.http"))
		goto end;

	int v = net->track->getval(d->trk, "out-copy");
	c->out_copy = (v != FMED_NULL);
	c->save_oncmd = (v == FMED_OUTCP_CMD);

	return c;

end:
	icy_close(c);
	return NULL;
}

static void icy_close(void *ctx)
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
static int icy_reset(icy *c, fmed_filt *d)
{
	ffstr s;
	void *http_ptr;
	if (0 != fmed_trk_filt_prev(d, &http_ptr))
		return FMED_RERR;

	ffstr ext;
	if (0 != http_findhdr(http_ptr, &ffhttp_shdr[FFHTTP_CONTENT_TYPE], &s)) {
		if (ffstr_ieqz(&s, "audio/mpeg"))
			ffstr_setz(&ext, "mp3");
		else if (ffstr_ieqz(&s, "audio/aacp"))
			ffstr_setz(&ext, "aac");
		else {
			errlog(d->trk, "unsupported Content-Type: %S", &s);
			return FMED_RERR;
		}
	} else {
		ffstr_setz(&ext, "mp3");
		dbglog(d->trk, "no Content-Type HTTP header in response, assuming MPEG");
	}

	if (c->next_filt_ext.len == 0) {
		const fmed_modinfo *mi;
		if (NULL == (mi = core->getmod2(FMED_MOD_INEXT, ext.ptr, ext.len))) {
			errlog(d->trk, "no module configured to open .%S stream", &ext);
			return FMED_RERR;
		}
		if (0 != net->track->cmd2(d->trk, FMED_TRACK_ADDFILT, mi->name))
			return FMED_RERR;
		c->next_filt_ext = ext;
	} else if (!ffstr_eq2(&c->next_filt_ext, &ext)) {
		errlog(d->trk, "unsupported behaviour: stream is changing audio format from %S to %S"
			, &c->next_filt_ext, &ext);
		return FMED_RERR;
	}

	uint meta_int = FFICY_NOMETA;
	if (0 != http_findhdr(http_ptr, &fficy_shdr[FFICY_HMETAINT], &s)) {
		uint n;
		if (ffstr_toint(&s, &n, FFS_INT32))
			meta_int = n;
		else
			warnlog(d->trk, "invalid value for HTTP response header %S: %S"
				, &fficy_shdr[FFICY_HMETAINT], &s);
	}
	fficy_parseinit(&c->icy, meta_int);

	// "netin" may be initialized if we've reconnected after I/O failure
	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c);
	return FMED_RDATA;
}

static int icy_process(void *ctx, fmed_filt *d)
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
		if (c->data.len == 0)
			return FMED_RMORE;

		size_t n = c->data.len;
		r = fficy_parse(&c->icy, c->data.ptr, &n, &s);
		ffstr_shift(&c->data, n);
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

static int icy_setmeta(icy *c, const ffstr *_data)
{
	fficymeta icymeta;
	ffstr artist = {0}, title = {0}, data = *_data, pair[2];
	fmed_que_entry *qent;
	int r;
	ffbool istitle = 0;

	dbglog(c->d->trk, "meta: [%L] %S", data.len, &data);
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
			errlog(c->d->trk, "bad metadata");
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
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);
	ffstr_acqstr3(&c->artist, &utf);

	ffstr_setcz(&pair[0], "title");
	ffutf8_strencode(&utf, title.ptr, title.len, FFU_WIN1252);
	ffstr_set2(&pair[1], &utf);
	net->qu->cmd2(FMED_QUE_METASET | ((FMED_QUE_TMETA | FMED_QUE_OVWRITE) << 16), qent, (size_t)pair);
	c->d->meta_changed = 1;
	ffstr_acqstr3(&c->title, &utf);

	if (c->netin != NULL && c->netin->fn_dyn) {
		netin_write(c->netin, NULL);
		c->netin = NULL;
	}

	if (c->out_copy && c->netin == NULL)
		c->netin = netin_create(c);
	return 0;
}


enum { IN_WAIT = 1, IN_DATANEXT };

static void* netin_create(icy *c)
{
	netin *n;
	void *trk;
	fmed_trk *trkconf;
	if (NULL == (n = ffmem_tcalloc1(netin)))
		return NULL;

	if (NULL == (trk = net->track->create(FMED_TRACK_NET, "")))
		return NULL;

	if (0 != net->track->cmd2(trk, FMED_TRACK_ADDFILT_BEGIN, "net.in"))
		return NULL;

	trkconf = net->track->conf(trk);
	trkconf->out_overwrite = c->d->out_overwrite;

	const char *output;
	output = net->track->getvalstr(c->d->trk, "out_filename");
	n->fn_dyn = (NULL != ffsz_findc(output, '$'));
	net->track->setvalstr(trk, "output", output);

	if (ffstr_eqcz(&c->next_filt_ext, "aac"))
		net->track->setvalstr(trk, "input", "?.aac");
	else
		net->track->setvalstr(trk, "input", "?.mp3");
	net->track->setval(trk, "netin_ptr", (size_t)n);
	n->c = c;

	if (c->save_oncmd)
		trkconf->out_file_del = 1;

	if (1 == net->track->getval(c->d->trk, "out_stream_copy"))
		trkconf->stream_copy = 1;

	net->track->setvalstr4(trk, "artist", (void*)&c->artist, FMED_TRK_META | FMED_TRK_VALSTR);
	net->track->setvalstr4(trk, "title", (void*)&c->title, FMED_TRK_META | FMED_TRK_VALSTR);

	c->d->track->cmd2(trk, FMED_TRACK_META_COPYFROM, c->d->trk);

	net->track->cmd(trk, FMED_TRACK_START);
	return n;
}

static void netin_write(netin *n, const ffstr *data)
{
	if (data == NULL) {
		n->fin = 1;
		n->c = NULL;
	} else
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
	if (n->c != NULL && n->c->netin == n)
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
	n->idx = ffint_cycleinc(n->idx, 2);
	if (n->fin)
		return FMED_RDONE;

	// get cmd from master track
	if (n->c->save_oncmd && n->c->d->save_trk) {
		n->c->d->save_trk = 0;
		d->out_file_del = 0;
	}

	return FMED_RDATA;
}
