/** URL.
Copyright (c) 2013 Simon Zolin
*/

#pragma once
#include "ipaddr.h"
#include "array.h"
#include <FFOS/socket.h>


/** URL structure. */
typedef struct ffurl {
	ushort offhost;
	ushort port; //number
	byte hostlen;
	byte portlen;

	ushort len;
	ushort offpath;
	ushort pathlen;
	ushort decoded_pathlen; //length of decoded filename

	unsigned idx : 4
		, ipv4 : 1
		, ipv6 : 1
		, querystr : 1 //has query string
		, complex : 1; //path contains %xx or "/." or "//"
} ffurl;

static inline void ffurl_init(ffurl *url) {
	memset(url, 0, sizeof(ffurl));
}

/** URL parsing error code. */
enum FFURL_E {
	FFURL_EOK ///< the whole input data has been processed
	, FFURL_EMORE ///< need more data
	, FFURL_ESTOP ///< unknown character has been met
	, FFURL_ETOOLARGE
	, FFURL_ESCHEME
	, FFURL_EIP6
	, FFURL_EHOST
	, FFURL_EPATH
};

/** Parse URL.
Return enum FFURL_E. */
FF_EXTERN int ffurl_parse(ffurl *url, const char *s, size_t len);

/** Get error message. */
FF_EXTERN const char *ffurl_errstr(int er);

/** URL component. */
enum FFURL_COMP {
	FFURL_FULLHOST // "host:8080"
	, FFURL_SCHEME // "http"
	, FFURL_HOST // "host"
	, FFURL_PORT
	, FFURL_PATH // "/file%20name"
	, FFURL_QS // "query%20string"
	, FFURL_PATHQS // "/file%20name?query%20string"
};

/** Get a component of the URL. */
FF_EXTERN ffstr ffurl_get(const ffurl *url, const char *base, int comp);

/** Check whether URL component exists. */
static inline ffbool ffurl_has(const ffurl *url, int comp)
{
	switch ((enum FFURL_COMP)comp) {
	case FFURL_HOST:
	case FFURL_FULLHOST:
		return url->hostlen != 0;
	case FFURL_SCHEME:
		return url->offhost != 0;
	case FFURL_PORT:
		return url->portlen != 0;
	case FFURL_PATH:
	case FFURL_PATHQS:
		return url->pathlen != 0;
	case FFURL_QS:
		return url->querystr;
	}
	return 0;
}

/**
Return address family;  0 if not an IP address;  -1 on error */
FF_EXTERN int ffurl_parse_ip(ffurl *u, const char *base, ffip6 *dst);

FF_EXTERN int ffip_parse(const char *ip, size_t len, ffip6 *dst);

/** Prepare a complete URL: "scheme://host[:port]/[path][querystr]"
'path' will be normalized but %XX-escaping won't be done.
If host is IPv6 address and port != 0, then host must be enclosed in "[]". */
FF_EXTERN int ffurl_joinstr(ffstr *dst, const ffstr *scheme, const ffstr *host, uint port, const ffstr *path, const ffstr *querystr);


typedef struct ffiplist {
	ffslice ip4; //ffip4[]
	ffslice ip6; //ffip6[]
} ffiplist;

typedef struct ffip_iter {
	uint idx;
	ffiplist *list;
	ffaddrinfo *ai;
} ffip_iter;

/** Set IP-list to a single address. */
static inline void ffip_list_set(ffiplist *l, uint family, const void *ip)
{
	ffslice *a = (family == AF_INET) ? &l->ip4 : &l->ip6;
	a->len = 1;
	a->ptr = (void*)ip;
}

/** Associate ffip_iter iterator with an IP-list and ffaddrinfo. */
#define ffip_iter_set(a, iplist, ainfo) \
do { \
	(a)->idx = 0; \
	(a)->list = iplist; \
	(a)->ai = ainfo; \
} while (0)

/** Get next address.
Return address family;  0 if no next address. */
FF_EXTERN int ffip_next(ffip_iter *it, void **ip);


/** Convert IP address to string
'port': optional parameter (e.g. "127.0.0.1:80", "[::1]:80"). */
FF_EXTERN uint ffip_tostr(char *buf, size_t cap, uint family, const void *ip, uint port);

enum FFADDR_FLAGS {
	FFADDR_USEPORT = 1
};

/** Convert IPv4/IPv6 address to string.
@flags: enum FFADDR_FLAGS. */
static inline size_t ffaddr_tostr(const ffaddr *a, char *dst, size_t cap, int flags) {
	const void *ip = (ffaddr_family(a) == AF_INET) ? (void*)&a->ip4.sin_addr : (void*)&a->ip6.sin6_addr;
	return ffip_tostr(dst, cap, ffaddr_family(a), ip, (flags & FFADDR_USEPORT) ? ffip_port(a) : 0);
}
