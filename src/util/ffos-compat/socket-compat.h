/**
Socket, network address, IPv4/IPv6 address functions.
Copyright (c) 2013 Simon Zolin
*/

#include <string.h>

#define ffskt  ffsock
#define FFSKT_SIGPIPE  FFSOCK_INIT_SIGPIPE
#define FFSKT_WSA  FFSOCK_INIT_WSA
#define FFSKT_WSAFUNCS  FFSOCK_INIT_WSAFUNCS
#define ffskt_init  ffsock_init
#define ffskt_sendv  ffsock_sendv
#define ffskt_fin  ffsock_fin
#define ffskt_nblock  ffsock_nonblock
#define ffskt_deferaccept  ffsock_deferaccept
#define ffskt_recv  ffsock_recv
#define ffskt_send  ffsock_send

#ifdef FF_UNIX
typedef void * ffsktopt_t;

#elif defined FF_WIN
typedef char * ffsktopt_t;
#endif

#ifdef FF_UNIX
#include <FFOS/file.h>

#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/uio.h>


enum {
	FF_BADSKT = -1
	, FF_MAXIP4 = 22
	, FF_MAXIP6 = 65 // "[addr]:port"
#ifdef FF_APPLE
	SOCK_NONBLOCK = 0x40000000,
#endif
};

#define ffskt_close  close


typedef struct addrinfo ffaddrinfo;

/** Translate name to network address.
Return 0 on success.
Example:
	ffaddrinfo *addrs;
	ffaddr_info(&addrs, "localhost", "80", AI_PASSIVE); */
static FFINL int ffaddr_info(ffaddrinfo **a, const char *host, const char *svc, int flags)
{
	ffaddrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM; //filter out results with the same IP but different ai_socktype
	hints.ai_flags = flags;
	return getaddrinfo(host, svc, &hints, a);
}

#define ffaddr_infoq ffaddr_info

/** Free ffaddrinfo structure. */
#define ffaddr_free freeaddrinfo

/** Get error message. */
static FFINL const char * ffaddr_errstr(int code, char *buf, size_t bufcap) {
	(void)buf; (void)bufcap;
	return gai_strerror(code);
}

/** Return TRUE if both IPv6 addresses are equal. */
#define ffip6_eq  IN6_ARE_ADDR_EQUAL

#else

#include <ws2tcpip.h>
#include <mswsock.h>

#include <FFOS/error.h>


#define FF_BADSKT  ((SOCKET)-1)

enum {
	FF_MAXIP4 = INET_ADDRSTRLEN

	, SOCK_NONBLOCK = 0x100
};

/** Accessors for ffiovec. */
#define iov_base  buf
#define iov_len  len

#define ffskt_sendv  ffsock_sendv

typedef struct sf_hdtr {
	ffiovec *headers;
	int hdr_cnt;
	ffiovec *trailers;
	int trl_cnt;
} sf_hdtr;


#define ffskt_close  closesocket


typedef ADDRINFOW ffaddrinfo;

static FFINL int ffaddr_infoq(ffaddrinfo **a, const ffsyschar *host, const ffsyschar *svc, int flags)
{
	ffaddrinfo hints = {};
	hints.ai_flags = flags;
	return GetAddrInfoW(host, svc, &hints, a);
}

FF_EXTN int ffaddr_info(ffaddrinfo **a, const char *host, const char *svc, int flags);

#define ffaddr_free  FreeAddrInfo

static FFINL const char * ffaddr_errstr(int code, char *buf, size_t bufcap) {
	fferr_str(code, buf, bufcap);
	return buf;
}

#define ffip6_eq  IN6_ADDR_EQUAL

#endif

typedef struct ffaddr {
	socklen_t len;
	union {
		struct sockaddr a;
		struct sockaddr_in ip4;
		struct sockaddr_in6 ip6;
	};
} ffaddr;

enum {
	FFADDR_MAXLEN = sizeof(struct sockaddr_in6)
};

/** Initialize ffaddr structure. */
static FFINL void ffaddr_init(ffaddr *adr) {
	memset(adr, 0, sizeof(ffaddr));
}

/** Copy address. */
static FFINL void ffaddr_copy(ffaddr *dst, const struct sockaddr *src, size_t len) {
	if (len > FFADDR_MAXLEN) {
		dst->len = 0;
		return;
	}
	memcpy(&dst->a, src, len);
	dst->len = (socklen_t)len;
}

/** Get address family. */
#define ffaddr_family(adr)  ((adr)->a.sa_family)

/** Set port (host byte order). */
static FFINL void ffip_setport(ffaddr *adr, uint port) {
	port = ffhton16((ushort)port);
	if (adr->a.sa_family == AF_INET)
		adr->ip4.sin_port = (short)port;
	else //if (adr->a.sa_family == AF_INET6)
		adr->ip6.sin6_port = (short)port;
}

/** Get port (host byte order). */
static FFINL ushort ffip_port(const ffaddr *adr) {
	return adr->a.sa_family == AF_INET
		? ffhton16(adr->ip4.sin_port)
		: ffhton16(adr->ip6.sin6_port);
}

/** Set IPv4 address. */
static FFINL void ffip4_set(ffaddr *adr, const struct in_addr *net_addr) {
	adr->ip4.sin_addr = *net_addr;
	adr->a.sa_family = AF_INET;
	adr->len = sizeof(struct sockaddr_in);
}

/** Set IPv4 address (host byte order).
Example:
ffip4_setint(&ip4, INADDR_LOOPBACK); */
static FFINL void ffip4_setint(ffaddr *adr, int net_addr) {
	union {
		struct in_addr a;
		uint i;
	} un;
	un.i = ffhton32(net_addr);
	ffip4_set(adr, &un.a);
}

/** Set IPv4-mapped address (host byte order). */
static FFINL void ffip6_setv4mapped(ffaddr *adr, int net_addr) {
	union {
		struct in6_addr *a;
		uint *i;
	} un;
	un.a = &adr->ip6.sin6_addr;
	un.i[0] = un.i[1] = 0;
	un.i[2] = ffhton32(0x0000ffff);
	un.i[3] = ffhton32(net_addr);
	adr->a.sa_family = AF_INET6;
	adr->len = sizeof(struct sockaddr_in6);
}

/** Set IPv6 address.
Example:
ffip6_set(&ip6, &in6addr_loopback); */
static FFINL void ffip6_set(ffaddr *adr, const struct in6_addr *net_addr) {
	adr->ip6.sin6_addr = *net_addr;
	adr->a.sa_family = AF_INET6;
	adr->len = sizeof(struct sockaddr_in6);
}

static FFINL void ffaddr_setip(ffaddr *a, uint family, const void *ip)
{
	if (family == AF_INET)
		ffip4_set(a, (struct in_addr*)ip);
	else
		ffip6_set(a, (struct in6_addr*)ip);
}

/** Set "any" address. */
static FFINL void ffaddr_setany(ffaddr *adr, int family) {
	if (family == AF_INET)
		ffip4_setint(adr, INADDR_ANY);
	else
		ffip6_set(adr, &in6addr_any);
}

/** Return TRUE for IPv4-mapped address. */
#define ffip6_isv4mapped(adr)  IN6_IS_ADDR_V4MAPPED(&(adr)->ip6.sin6_addr)

/** Convert IPv4-mapped address to IPv4. */
static FFINL void ffip_v4mapped_tov4(ffaddr *a) {
	union {
		struct in6_addr *a6;
		struct in_addr *a;
		char *b;
	} un;
	a->ip4.sin_port = a->ip6.sin6_port;
	un.a6 = &a->ip6.sin6_addr;
	un.b += 12;
	ffip4_set(a, un.a);
}


#define ffskt_create  ffsock_create

/** Initiate a connection on a socket. */
static FFINL int ffskt_connect(ffskt sk, const struct sockaddr *addr, size_t addrlen) {
	return connect(sk, addr, (socklen_t)addrlen);
}

#define ffskt_listen  ffsock_listen

#define ffskt_setopt  ffsock_setopt

#define ffskt_getopt  ffsock_getopt

/** Bind a name to a socket. */
static FFINL int ffskt_bind(ffskt sk, const struct sockaddr *addr, size_t addrlen) {
	(void)ffskt_setopt(sk, SOL_SOCKET, SO_REUSEADDR, 1);
	return bind(sk, addr, (socklen_t)addrlen);
}

/** Get local socket address.
Return 0 on success. */
static FFINL int ffskt_local(ffskt sk, ffaddr *adr) {
	socklen_t size = FFADDR_MAXLEN;
	if (0 != getsockname(sk, &adr->a, &size)
		|| size > FFADDR_MAXLEN)
		return -1;
	adr->len = size;
	return 0;
}
