/** HTTP client
This interface provides an easy way to retrieve a document via HTTP.
2019, Simon Zolin
*/

#pragma once

#include "http1.h"
#include <FFOS/timerqueue.h>


/** Deinitialize recycled connection objects (on kqueue close). */
FF_EXTERN void ffhttpcl_deinit();


enum FFHTTPCL_F {
	FFHTTPCL_NOREDIRECT = 2, /** Don't follow redirections. */
};

enum FFHTTPCL_LOG {
	FFHTTPCL_LOG_ERR = 1,
	FFHTTPCL_LOG_WARN,
	FFHTTPCL_LOG_USER,
	FFHTTPCL_LOG_INFO,
	FFHTTPCL_LOG_DEBUG,

	FFHTTPCL_LOG_SYS = 0x10,
};

/** User's handler receives events when the internal state changes. */
typedef void (*ffhttpcl_handler)(void *param);

/** Logger function. */
typedef void (*ffhttpcl_log)(void *udata, ffuint level, const char *fmt, ...);

/** Set a one-shot timer.
value_ms: timer value in milliseconds;  0: disable. */
typedef void (*ffhttpcl_timer)(fftimerqueue_node *tmr, ffuint value_ms);

/** HTTP client configuration. */
struct ffhttpcl_conf {
	fffd kq; /** Kernel queue used for asynchronous events.  Required. */
	ffhttpcl_log log;
	ffhttpcl_timer timer;
	ffuint nbuffers;
	ffuint buffer_size;
	ffuint buffer_lowat;
	ffuint connect_timeout; /** msec */
	ffuint timeout; /** msec */
	ffuint max_redirect; /** Maximum times to follow redirections. */
	ffuint max_reconnect; /** Maximum times to reconnect after I/O failure. */
	struct {
		/** Proxy hostname (static string).
		NULL: no proxy */
		const char *host;
		ffuint port; /** Proxy port */
	} proxy;
	ffuint debug_log :1; /** Log messages with FFHTTPCL_LOG_DEBUG. */
};

enum FFHTTPCL_CONF_F {
	FFHTTPCL_CONF_GET,
	FFHTTPCL_CONF_SET,
};

enum FFHTTPCL_ST {
	// all errors are <0
	FFHTTPCL_ENOADDR = -2,
	FFHTTPCL_ERR = -1,

	FFHTTPCL_DONE = 0,
	FFHTTPCL_DNS_WAIT, /** resolving hostname via DNS */
	FFHTTPCL_IP_WAIT, /** connecting to host */
	FFHTTPCL_REQ_WAIT, /** sending request */
	FFHTTPCL_RESP_WAIT, /** receiving response (HTTP headers) */
	FFHTTPCL_RESP, /** received response headers */
	FFHTTPCL_RESP_RECV, /** receiving data */
};

/** Create HTTP request.
flags: enum FFHTTPCL_F
Return connection object. */
FF_EXTERN void* ffhttpcl_request(const char *method, const char *url, ffuint flags);

/** Close connection. */
FF_EXTERN void ffhttpcl_close(void *con);

/** Set asynchronous callback function.
User function is called every time the connection status changes.
Processing is suspended until user calls send(). */
FF_EXTERN void ffhttpcl_sethandler(void *con, ffhttpcl_handler func, void *udata);

/** Connect, send request, receive response.
Note: data must be NULL - sending request body isn't supported. */
FF_EXTERN void ffhttpcl_send(void *con, const ffstr *data);

/** Parsed headers information. */
typedef struct ffhttp_headers {
	ffushort len;
	ffushort firstline_len;
	ffbyte http11 : 1
		, conn_close : 1
		, has_body : 1
		, chunked : 1 ///< Transfer-Encoding: chunked
		, body_conn_close : 1 // for response
		;
	// ffbyte ce_gzip : 1 ///< Content-Encoding: gzip
	// 	, ce_identity : 1 ///< no Content-Encoding or Content-Encoding: identity
	// 	;
	int64 cont_len; ///< Content-Length value or -1

	ffstr raw_headers;
} ffhttp_headers;

/** Get header value.
Return 0 if header is not found. */
static inline int ffhttp_findhdr(const ffhttp_headers *h, const char *name, size_t namelen, ffstr *dst)
{
	ffstr d = h->raw_headers;
	for (;;) {
		ffstr nm, val;
		int r = http_hdr_parse(d, &nm, &val);
		if (r <= 0)
			break;

		ffstr_shift(&d, r);
		if (r <= 2)
			break;
		if (ffstr_ieq(&nm, name, namelen)) {
			*dst = val;
			return 1;
		}
	}
	return 0;
}

/** Parsed response. */
typedef struct ffhttp_response {
	ffhttp_headers h;

	ffuint code;
	ffstr status;
	ffstr content_type;
} ffhttp_response;

/** Initialize response parser. */
static FFINL void ffhttp_resp_init(ffhttp_response *r) {
	memset(r, 0, sizeof(ffhttp_response));
	r->h.cont_len = -1;
	// r->h.ce_identity = 1;
}

/** Get response data.
@data: response body
Return enum FFHTTPCL_ST. */
FF_EXTERN int ffhttpcl_recv(void *con, ffhttp_response **resp, ffstr *data);

/** Add request header. */
FF_EXTERN void ffhttpcl_header(void *con, const ffstr *name, const ffstr *val, ffuint flags);

/** Configure connection object.
May be called only before the first send().
flags: enum FFHTTPCL_CONF_F */
FF_EXTERN void ffhttpcl_conf(void *con, struct ffhttpcl_conf *conf, ffuint flags);
