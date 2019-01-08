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
	uint conn_tmout;
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
	void *trk;
	ffarr d[2];
	uint idx;
	uint fin :1;
	uint fn_dyn :1;
	icy *c;
} netin;

struct filter {
	const struct ffhttp_filter *iface;
	void *p;
};

typedef struct nethttp {
	uint state;
	fmed_filt *d;

	char *method;
	fflist1_item recycled;
	ffstr orig_target_url; // original target URL
	ffarr target_url; // target URL (possibly relocated)
	ffarr hostname; // server hostname (may be a proxy)
	uint hostport; // server port (may be a proxy)
	ffurl url;
	ffiplist iplist;
	ffip6 ip;
	ffaddrinfo *addr;
	ffip_iter curaddr;
	ffskt sk;
	ffaio_task aio;
	uint reconnects;
	fftmrq_entry tmr;
	ffhttp_cook hdrs;

	ffstr *bufs;
	uint rbuf;
	uint wbuf;
	size_t curbuf_len;
	uint lowat; //low-watermark number of filled bytes for buffer
	ffstr data, outdata;

	ffstr hbuf;
	ffhttp_response resp;
	uint nredirect;
	uint max_reconnect;

	uint flags;
	uint buflock :1
		, iowait :1 //waiting for I/O, all input data is consumed
		, async :1
		, preload :1 //fill all buffers
		, proxy :1 // connect via a proxy server
		;

	fftask_handler handler;
	void *udata;
	uint status;
	struct filter f;
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
static int tcp_ioerr(nethttp *c);

//HTTP
static int http_config(ffpars_ctx *ctx);
static void* httpcli_open(fmed_filt *d);
static int httpcli_process(void *ctx, fmed_filt *d);
static void httpcli_close(void *ctx);
static const fmed_filter fmed_http = {
	&httpcli_open, &httpcli_process, &httpcli_close
};

static int http_conf_done(ffparser_schem *p, void *obj);
static int http_prepreq(nethttp *c, ffstr *dst);
static int http_parse(nethttp *c);
static int http_recv(nethttp *c, uint tcpfin);

// HTTP IFACE
static void* http_if_request(const char *method, const char *url, uint flags);
static void http_if_close(void *con);
static void http_if_sethandler(void *con, fftask_handler func, void *udata);
static void http_if_send(void *con, const ffstr *data);
static int http_if_recv(void *con, ffhttp_response **resp, ffstr *data);
static void http_if_header(void *con, const ffstr *name, const ffstr *val, uint flags);
static const fmed_net_http http_iface = {
	&http_if_request, &http_if_close, &http_if_sethandler, &http_if_send, &http_if_recv, &http_if_header,
};

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
	{ "connect_timeout",	FFPARS_TINT,  FFPARS_DSTOFF(net_conf, conn_tmout) },
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
	else if (!ffsz_cmp(name, "httpif"))
		return &http_iface;
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


static int buf_alloc(nethttp *c, size_t size);

static const struct ffhttp_filter*const filters[] = {
	&ffhttp_chunked_filter, &ffhttp_contlen_filter, &ffhttp_connclose_filter
};

/** Prepare a normal request URL. */
static int http_prep_url(nethttp *c, const char *url)
{
	int r;
	ffurl u;
	ffstr s;
	ffstr_setz(&s, url);
	ffurl_init(&u);
	if (0 != (r = ffurl_parse(&u, s.ptr, s.len))) {
		errlog(c->d->trk, "URL parse: %S: %s"
			, &s, ffurl_errstr(r));
		return -1;
	}
	if (!ffurl_has(&u, FFURL_HOST)) {
		errlog(c->d->trk, "incorrect URL: %S", &s);
		return -1;
	}
	ffstr scheme = ffurl_get(&u, s.ptr, FFURL_SCHEME);
	if (scheme.len == 0)
		ffstr_setz(&scheme, "http");
	if (u.port == FFHTTP_PORT && ffstr_eqz(&scheme, "http"))
		u.port = 0;
	ffstr host = ffurl_get(&u, s.ptr, FFURL_HOST);
	ffstr path = ffurl_get(&u, s.ptr, FFURL_PATH);
	ffstr qs = ffurl_get(&u, s.ptr, FFURL_QS);
	if (0 == ffurl_joinstr(&c->orig_target_url, &scheme, &host, u.port, &path, &qs))
		return -1;

	return 0;
}

static void* http_if_request(const char *method, const char *url, uint flags)
{
	nethttp *c;
	if (NULL != (c = (void*)fflist1_pop(&net->recycled_cons)))
		c = FF_GETPTR(nethttp, recycled, c);
	else if (NULL == (c = ffmem_new(nethttp)))
		return NULL;
	c->d = ffmem_new(fmed_filt); //logger needs c->d->trk
	c->sk = FF_BADSKT;
	ffhttp_respinit(&c->resp);
	ffhttp_cookinit(&c->hdrs, NULL, 0);

	if (0 != http_prep_url(c, url))
		goto done;
	ffstr_set2(&c->target_url, &c->orig_target_url);

	if (!ffhttp_check_method(method, ffsz_len(method))) {
		errlog(c->d->trk, "invalid HTTP method: %s", method);
		goto done;
	}
	if (NULL == (c->method = ffsz_alcopyz(method)))
		goto done;

	if (NULL == (c->bufs = ffmem_callocT(net->conf.nbufs, ffstr))) {
		goto done;
	}
	if (0 != buf_alloc(c, net->conf.bufsize))
		goto done;

	c->tmr.handler = &tcp_ontmr;
	c->tmr.param = c;
	c->max_reconnect = net->conf.max_reconnect;
	c->flags = flags;

	return c;

done:
	http_if_close(c);
	return NULL;
}

static void http_if_close(void *con)
{
	nethttp *c = con;
	core->timer(&c->tmr, 0, 0);

	if (c->f.p != NULL)
		c->f.iface->close(c->f.p);

	FF_SAFECLOSE(c->addr, NULL, ffaddr_free);
	if (c->sk != FF_BADSKT) {
		ffskt_fin(c->sk);
		ffskt_close(c->sk);
		c->sk = FF_BADSKT;
	}
	ffarr_free(&c->target_url);
	ffstr_free(&c->orig_target_url);
	ffmem_free(c->method);
	ffaio_fin(&c->aio);
	ffhttp_respfree(&c->resp);
	ffhttp_cookdestroy(&c->hdrs);

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

static void http_if_sethandler(void *con, fftask_handler func, void *udata)
{
	nethttp *c = con;
	c->handler = func;
	c->udata = udata;
}

enum {
	I_START, I_ADDR, I_NEXTADDR, I_CONN,
	I_HTTP_REQ, I_HTTP_REQ_SEND, I_HTTP_RESP, I_HTTP_RESP_PARSE, I_HTTP_RECVBODY, I_HTTP_RESPBODY,
	I_DONE, I_ERR, I_NOOP,
};

static void call_handler(nethttp *c, uint status)
{
	c->status = status;
	c->handler(c->udata);
}

static void http_if_process(nethttp *c)
{
	int r;
	ffaddr a = {0};

	for (;;) {
	switch (c->state) {

	case I_START:
		c->state = I_ADDR;
		call_handler(c, FMED_NET_DNS_WAIT);
		return;

	case I_ADDR:
		if (0 != ip_resolve(c)) {
			c->state = I_ERR;
			continue;
		}
		c->state = I_NEXTADDR;
		call_handler(c, FMED_NET_IP_WAIT);
		return;

	case I_NEXTADDR:
		if (0 != tcp_prepare(c, &a)) {
			c->state = I_ERR;
			continue;
		}
		c->state = I_CONN;
		// fall through

	case I_CONN:
		r = tcp_connect(c, &a.a, a.len);
		if (r == FMED_RASYNC)
			return;
		else if (r == FMED_RMORE) {
			c->state = I_NEXTADDR;
			continue;
		}
		c->state = I_HTTP_REQ;
		call_handler(c, FMED_NET_REQ_WAIT);
		return;

	case I_HTTP_REQ:
		http_prepreq(c, &c->data);
		c->state = I_HTTP_REQ_SEND;
		// fall through

	case I_HTTP_REQ_SEND:
		r = tcp_send(c);
		if (r == FMED_RASYNC)
			return;
		else if (r == FMED_RERR) {
			tcp_ioerr(c);
			continue;
		}

		dbglog(c->d->trk, "receiving response...");
		ffstr_set(&c->data, c->bufs[0].ptr, net->conf.bufsize);
		c->state = I_HTTP_RESP;
		call_handler(c, FMED_NET_RESP_WAIT);
		return;

	case I_HTTP_RESP:
		r = tcp_recvhdrs(c);
		if (r == FMED_RASYNC)
			return;
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
			c->state = I_ERR;
			continue;
		case 1:
			c->state = I_HTTP_RESP;
			continue;
		case 2:
			if (c->flags & FMED_NET_NOREDIRECT)
				break;
			c->state = I_ADDR;
			continue;
		}

		if (c->resp.h.has_body) {
			const struct ffhttp_filter *const *f;
			FFARRS_FOREACH(filters, f) {
				void *d = (*f)->open(&c->resp.h);
				if (d == NULL)
				{}
				else if (d == (void*)-1) {
					warnlog(c->d->trk, "filter #%u open error", (int)(f - filters));
					c->state = I_ERR;
					continue;
				} else {
					dbglog(c->d->trk, "opened filter #%u", (int)(f - filters));
					c->f.iface = *f;
					c->f.p = d;
					break;
				}
			}
		}

		ffstr_set2(&c->data, &c->bufs[0]);
		ffstr_shift(&c->data, c->resp.h.len);
		c->bufs[0].len = 0;
		if (c->resp.h.has_body)
			c->state = I_HTTP_RESPBODY;
		else
			c->state = I_DONE;
		call_handler(c, FMED_NET_RESP);
		return;

	case I_HTTP_RECVBODY:
		r = tcp_recv(c);
		if (r == FMED_RASYNC)
			return;
		else if (r == FMED_RERR) {
			tcp_ioerr(c);
			continue;
		} else if (r == FMED_RDONE) {
			r = http_recv(c, 1);
			switch (r) {
			case -1:
				c->state = I_ERR;
				continue;
			case 0:
				c->state = I_DONE;
				continue;
			}
			c->state = I_DONE;
			continue;
		} else if (r == FMED_RMORE) {
			c->state = I_ERR;
			continue;
		}
		c->data = c->bufs[c->rbuf];
		c->bufs[c->rbuf].len = 0;
		c->rbuf = ffint_cycleinc(c->rbuf, net->conf.nbufs);
		c->state = I_HTTP_RESPBODY;
		// fall through

	case I_HTTP_RESPBODY:
		r = http_recv(c, 0);
		switch (r) {
		case -1:
			c->state = I_ERR;
			continue;
		case 0:
			c->state = I_DONE;
			continue;
		}
		if (c->data.len == 0)
			c->state = I_HTTP_RECVBODY;
		call_handler(c, FMED_NET_RESP_RECV);
		return;

	case I_ERR:
	case I_DONE: {
		uint r = (c->state == I_ERR) ? FMED_NET_ERR : FMED_NET_DONE;
		c->state = I_NOOP;
		call_handler(c, r);
		return;
	}

	case I_NOOP:
		return;
	}
	}
}

static int http_recv(nethttp *c, uint tcpfin)
{
	int r;
	ffstr s;
	if (!tcpfin)
		r = c->f.iface->process(c->f.p, &c->data, &s);
	else
		r = c->f.iface->process(c->f.p, NULL, &s);
	if (ffhttp_iserr(r)) {
		warnlog(c->d->trk, "filter error: (%d) %s", r, ffhttp_errstr(r));
		return -1;
	}
	c->outdata = s;
	if (r == FFHTTP_DONE)
		return 0;
	return 1;
}

static void http_if_send(void *con, const ffstr *data)
{
	http_if_process(con);
}

static int http_if_recv(void *con, ffhttp_response **resp, ffstr *data)
{
	nethttp *c = con;
	*resp = &c->resp;
	ffstr_set2(data, &c->outdata);
	c->outdata.len = 0;
	return c->status;
}

static void http_if_header(void *con, const ffstr *name, const ffstr *val, uint flags)
{
	nethttp *c = con;
	ffhttp_addhdr_str(&c->hdrs, name, val);
}


static int http_config(ffpars_ctx *ctx)
{
	net->conf.bufsize = 16 * 1024;
	net->conf.nbufs = 2;
	net->conf.buf_lowat = 8 * 1024;
	net->conf.conn_tmout = 1500;
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

static int ip_resolve(nethttp *c)
{
	char *hostz;
	int r;

	ffurl_init(&c->url);
	if (0 != (r = ffurl_parse(&c->url, c->target_url.ptr, c->target_url.len))) {
		errlog(c->d->trk, "URL parse: %S: %s"
			, &c->target_url, ffurl_errstr(r));
		goto done;
	}
	if (c->url.port == 0)
		c->url.port = FFHTTP_PORT;

	if (!c->proxy) {
		ffstr host = ffurl_get(&c->url, c->target_url.ptr, FFURL_HOST);
		ffstr_set2(&c->hostname, &host);
		c->hostport = c->url.port;
		r = ffurl_parse_ip(&c->url, c->target_url.ptr, &c->ip);
	}

	if (r < 0) {
		errlog(c->d->trk, "bad IP address: %S", &c->hostname);
		goto done;
	} else if (r != 0) {
		ffip_list_set(&c->iplist, r, &c->ip);
		ffip_iter_set(&c->curaddr, &c->iplist, NULL);
		return 0;
	}

	if (NULL == (hostz = ffsz_alcopystr(&c->hostname))) {
		syserrlog(c->d->trk, "%s", ffmem_alloc_S);
		goto done;
	}

	infolog(c->d->trk, "resolving host %S...", &c->hostname);
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
		ffip_setport(a, c->hostport);

		char saddr[FF_MAXIP6];
		size_t n = ffaddr_tostr(a, saddr, sizeof(saddr), FFADDR_USEPORT);
		infolog(c->d->trk, "connecting to %S (%*s)...", &c->hostname, n, saddr);

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
		fffd kq = core->kq;
		if (c->d->trk != NULL)
			kq = (fffd)net->track->cmd(c->d->trk, FMED_TRACK_KQ);
		if (0 != ffaio_attach(&c->aio, kq, FFKQU_READ | FFKQU_WRITE)) {
			syserrlog(c->d->trk, "%s", ffkqu_attach_S);
			return -1;
		}
		return 0;
	}

	errlog(c->d->trk, "no next address to connect");
	c->d->e_no_source = 1;
	return -1;
}

static void tcp_aio(void *udata)
{
	nethttp *c = udata;
	c->async = 0;
	core->timer(&c->tmr, 0, 0);
	http_if_process(c);
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
		core->timer(&c->tmr, -(int)net->conf.conn_tmout, 0);
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
	http_if_process(c);
}

static int tcp_recvhdrs(nethttp *c)
{
	ssize_t r;

	if (c->bufs[0].len == net->conf.bufsize) {
		errlog(c->d->trk, "too large response headers [%L]"
			, c->bufs[0].len);
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

	ffarr_free(&c->target_url);
	ffstr_set2(&c->target_url, &c->orig_target_url);
	ffmem_tzero(&c->url);
	ffmem_tzero(&c->iplist);
	ffmem_tzero(&c->ip);

	for (uint i = 0;  i != net->conf.nbufs;  i++) {
		c->bufs[i].len = 0;
	}
	c->curbuf_len = 0;
	c->wbuf = c->rbuf = 0;
	c->lowat = 0;

	ffhttp_respfree(&c->resp);
	ffhttp_respinit(&c->resp);
	c->state = I_ADDR;
	dbglog(c->d->trk, "reconnecting...", 0);
	return 0;
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
		r = ffaio_recv(&c->aio, &tcp_aio, c->bufs[c->wbuf].ptr + c->curbuf_len, net->conf.bufsize - c->curbuf_len);
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

	if (c->flags & FMED_NET_HTTP10)
		ffstr_setcz(&ck.proto, "HTTP/1.0");

	s = ffurl_get(&c->url, c->target_url.ptr, FFURL_PATHQS);
	ffhttp_addrequest(&ck, c->method, ffsz_len(c->method), s.ptr, s.len);

	s = ffurl_get(&c->url, c->target_url.ptr, FFURL_FULLHOST);
	ffhttp_addihdr(&ck, FFHTTP_HOST, s.ptr, s.len);

	ffarr_append(&ck.buf, c->hdrs.buf.ptr, c->hdrs.buf.len);

	if (net->conf.user_agent != 0) {
		ffstr_setz(&s, http_ua[net->conf.user_agent - 1]);
		ffhttp_addihdr(&ck, FFHTTP_USERAGENT, s.ptr, s.len);
	}
	ffhttp_cookfin(&ck);
	ffstr_acqstr3(&c->hbuf, &ck.buf);
	ffstr_set2(dst, &c->hbuf);
	ffhttp_cookdestroy(&ck);
	dbglog(c->d->trk, "sending request: %S", dst);
	return 0;
}

/**
Return 0 on success;  1 - need more data;  2 - redirect;  -1 on error. */
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
		ffarr_free(&c->target_url);
		if (0 == ffstr_fmt(&c->target_url, "%S", &s)) {
			syserrlog(c->d->trk, "%s", ffmem_alloc_S);
			return -1;
		}
		ffurl_init(&c->url);
		c->bufs[0].len = 0;
		ffhttp_respfree(&c->resp);
		ffhttp_respinit(&c->resp);
		return 2;
	}

	return 0;
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

	int v = net->track->getval(d->trk, "out-copy");
	c->out_copy = (v != FMED_NULL);
	c->save_oncmd = (v == FMED_OUTCP_CMD);

	const char *s = net->track->getvalstr(d->trk, "icy_format");
	ffstr_setz(&c->next_filt_ext, s);

	return c;
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
	fficy_parseinit(&c->icy, fmed_getval("icy_meta_int"));

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
	else if (ffstr_eqcz(&c->next_filt_ext, "ogg"))
		net->track->setvalstr(trk, "input", "?.ogg");
	else
		net->track->setvalstr(trk, "input", "?.mp3");
	net->track->setval(trk, "netin_ptr", (size_t)n);
	n->c = c;

	if (c->save_oncmd) {
		net->track->setval(trk, "out_bufsize", 2 * 1024 * 1024);
		trkconf->out_file_del = 1;
	}

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
		net->track->cmd(n->trk, FMED_TRACK_WAKE);
}

static void* netin_open(fmed_filt *d)
{
	netin *n;
	n = (void*)fmed_getval("netin_ptr");
	n->trk = d->trk;
	n->state = IN_DATANEXT;
	return n;
}

static void netin_close(void *ctx)
{
	netin *n = ctx;
	ffarr_free(&n->d[0]);
	ffarr_free(&n->d[1]);
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


struct httpclient {
	void *con;
	void *trk;
	uint st;
	uint status;
	ffstr data;
	ffstr next_filt_ext;
	fmed_filt *d;
};

static void* httpcli_open(fmed_filt *d)
{
	struct httpclient *c;
	if (NULL == (c = ffmem_new(struct httpclient)))
		return NULL;
	c->trk = d->trk;
	c->d = d;
	return c;
}

static void httpcli_close(void *ctx)
{
	struct httpclient *c = ctx;
	http_iface.close(c->con);
	ffmem_free(c);
}

/** Process response: add appropriate filters to the chain */
static int httpcli_resp(struct httpclient *c, ffhttp_response *resp)
{
	if (resp->code != 200) {
		ffstr ln = ffhttp_respstatus(resp);
		errlog(c->trk, "resource unavailable: %S", &ln);
		return FMED_RERR;
	}

	ffstr s, ext;
	if (0 != ffhttp_findihdr(&resp->h, FFHTTP_CONTENT_TYPE, &s)) {
		if (ffstr_ieqz(&s, "audio/mpeg"))
			ffstr_setz(&ext, "mp3");
		else if (ffstr_ieqz(&s, "audio/aac") || ffstr_ieqz(&s, "audio/aacp"))
			ffstr_setz(&ext, "aac");
		else if (ffstr_ieqz(&s, "application/ogg"))
			ffstr_setz(&ext, "ogg");
		else {
			errlog(c->trk, "unsupported Content-Type: %S", &s);
			return FMED_RERR;
		}
	} else {
		ffstr_setz(&ext, "mp3");
		dbglog(c->trk, "no Content-Type HTTP header in response, assuming MPEG");
	}

	if (c->next_filt_ext.len != 0) {

		if (!ffstr_eq2(&c->next_filt_ext, &ext)) {
			errlog(c->trk, "unsupported behaviour: stream is changing audio format from %S to %S"
				, &c->next_filt_ext, &ext);
			return FMED_RERR;
		}
		return FMED_RDATA;
	}

	const fmed_modinfo *mi;
	if (NULL == (mi = core->getmod2(FMED_MOD_INEXT, ext.ptr, ext.len))) {
		errlog(c->trk, "no module configured to open .%S stream", &ext);
		return FMED_RERR;
	}
	if (0 == net->track->cmd(c->trk, FMED_TRACK_FILT_ADD, mi->name))
		return FMED_RERR;
	c->next_filt_ext = ext;

	s = fficy_shdr[FFICY_HMETAINT];
	if (0 != ffhttp_findhdr(&resp->h, s.ptr, s.len, &s)) {
		uint n;
		if (ffstr_toint(&s, &n, FFS_INT32)) {
			if (0 == net->track->cmd(c->trk, FMED_TRACK_FILT_ADD, "net.icy"))
				return FMED_RERR;
			net->track->setvalstr(c->trk, "icy_format", ext.ptr);
			net->track->setval(c->trk, "icy_meta_int", n);
		} else {
			warnlog(c->trk, "invalid value for HTTP response header %S: %S"
				, &fficy_shdr[FFICY_HMETAINT], &s);
		}
	}


	return FMED_RDATA;
}

/** Handle events from 'httpif'. */
static void httpcli_handler(void *param)
{
	struct httpclient *c = param;
	ffhttp_response *resp;
	ffstr data;
	int r = http_iface.recv(c->con, &resp, &data);
	c->status = r;

	switch ((enum FMED_NET_ST)r) {

	case FMED_NET_RESP:
		c->d->net_reconnect = 1;
		r = httpcli_resp(c, resp);
		if (r == FMED_RERR) {
			c->st = 3;
			net->track->cmd(c->trk, FMED_TRACK_WAKE);
			return;
		}
		break;

	case FMED_NET_RESP_RECV:
		c->data = data;
		//fallthrough
	case FMED_NET_DONE:
	case FMED_NET_ERR:
		net->track->cmd(c->trk, FMED_TRACK_WAKE);
		return;

	default:
		break;
	}

	http_iface.send(c->con, NULL);
}

/**
Make request via 'httpif'.
Get data from 'httpif' and pass further through the chain. */
static int httpcli_process(void *ctx, fmed_filt *d)
{
	struct httpclient *c = ctx;

	if (d->flags & FMED_FSTOP) {
		d->outlen = 0;
		return FMED_RDONE;
	}

	switch (c->st) {
	case 0: {
		const char *url = d->track->getvalstr(d->trk, "input");
		if (NULL == (c->con = http_iface.request("GET", url, 0)))
			return FMED_RERR;

		core->getmod("net.icy"); // load net.icy config
		if (net->conf.meta) {
			ffstr val;
			ffstr_setz(&val, "1");
			http_iface.header(c->con, &fficy_shdr[FFICY_HMETADATA], &val, 0);
		}

		http_iface.sethandler(c->con, &httpcli_handler, c);
		c->st = 1;
		http_iface.send(c->con, NULL);
		return FMED_RASYNC;
	}

	case 1:
		switch (c->status) {
		case FMED_NET_RESP_RECV:
			c->st = 2;
			d->out = c->data.ptr,  d->outlen = c->data.len;
			return FMED_RDATA;

		case FMED_NET_DONE:
			d->outlen = 0;
			return FMED_RDONE;
		}
		return FMED_RERR;

	case 2:
		c->st = 1;
		http_iface.send(c->con, NULL);
		return FMED_RASYNC;

	case 3:
		return FMED_RERR;
	}

	return FMED_RERR;
}
