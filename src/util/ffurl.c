/**
Copyright (c) 2013 Simon Zolin
*/

#include "url.h"
#include "path.h"


/*
host:
	host.com
	127.0.0.1
	[::1]

valid input:
	[http://] host [:80] [/path [?query]]
	/path [?query]
*/
int ffurl_parse(ffurl *url, const char *s, size_t len)
{
	enum {
		iHostStart = 0, iHost, iIp6, iAfterHost, iPort
		, iPathStart, iPath, iQuoted1, iQuoted2, iQs
		, iPortOrScheme, iSchemeSlash2
	};
	int er = 0;
	int idx = url->idx;
	size_t i;
	ffbool again = 0;
	ffbool consistent;

	for (i = url->len;  i < len;  i++) {
		int ch = s[i];

		switch (idx) {
		case iSchemeSlash2:
			if (ch != '/') {
				er = FFURL_ESCHEME;
				break;
			}
			// http://
			url->hostlen = 0;
			idx = iHostStart;
			url->offhost = (ushort)i + 1;
			break;

		case iIp6:
			if (i - url->offhost > 45 + FFSLEN("[]")) {
				er = FFURL_ETOOLARGE;
				break;
			}
			if (ch == ']') {
				idx = iAfterHost;
			}
			else if (!(ffchar_ishex(ch) || ch == ':' || ch == '%')) {// valid ip6 addr: 0-9a-f:%
				er = FFURL_EIP6;
				break;
			}
			url->hostlen++;
			break;

		case iHostStart:
			if (ch == '[') {
				// [::1
				idx = iIp6;
				url->hostlen++;
				url->ipv6 = 1;
				break;
			}
			else if (ffchar_isdigit(ch))
				url->ipv4 = 1;
			else if (ch == '/') {
				idx = iPathStart;
				again = 1;
				break;
			}
			// host or 127.
			idx = iHost;
			//break;

		case iHost:
			if (ffchar_isname(ch) || ch == '-' || ch == '.') { // a-zA-Z0-9_\-\.
				if (i - url->offhost > 0xff) {
					er = FFURL_ETOOLARGE;
					break;
				}
				if (url->ipv4 && !ffchar_isdigit(ch) && ch != '.')
					url->ipv4 = 0;
				url->hostlen++;
				break;
			}
			if (url->hostlen == 0) {
				er = FFURL_EHOST;
				break; //host can not be empty
			}
			idx = iAfterHost;
			//break;

		case iAfterHost:
			if (ch == ':') {
				// host: or http:
				idx = (url->offhost == 0 ? iPortOrScheme : iPort);
			}
			else {
				idx = iPathStart;
				again = 1;
			}
			break;

		case iPortOrScheme:
			if (ch == '/') {
				idx = iSchemeSlash2;
				break;
			}
			idx = iPort;
			//break;

		case iPort:
			{
				uint p;
				if (!ffchar_isdigit(ch)) {
					idx = iPathStart;
					again = 1;
					break;
				}
				p = (uint)url->port * 10 + (ch - '0');
				if (p > 0xffff) {
					er = FFURL_ETOOLARGE;
					break;
				}
				url->port = (ushort)p;
			}
			url->portlen++;
			break;

		case iPathStart:
			if (ch != '/') {
				er = FFURL_ESTOP;
				break;
			}
			url->offpath = (ushort)i;
			idx = iPath;
			//break;

		case iPath:
			if (ch == '%') {
				if (!url->complex)
					url->complex = 1;
				idx = iQuoted1;
			}
			else if (ch == '?') {
				idx = iQs;
				break;
			}
			else if (ch == '/' || ch == '.') {
				if (!url->complex && i != 0 && s[i - 1] == '/')
					url->complex = 1; //handle "//" and "/./", "/." and "/../", "/.."
			}
			else if (ffchar_isansiwhite(ch) || ch == '#') {
				er = FFURL_ESTOP;
				break;
			}
			url->decoded_pathlen++;
			url->pathlen++;
			break;

		case iQuoted1:
		case iQuoted2:
			if (!ffchar_ishex(ch)) {
				er = FFURL_EPATH;
				break;
			}
			idx = (idx == iQuoted1 ? iQuoted2 : iPath);
			url->pathlen++;
			break;

		case iQs:
			if (ffchar_isansiwhite(ch) || ch == '#') {
				er = FFURL_ESTOP;
				break;
			}
			if (!url->querystr)
				url->querystr = 1;
			break;
		}

		if (er != 0)
			goto fin;

		if (again) {
			again = 0;
			i--;
			continue;
		}
	}

	consistent = ((idx == iHost && url->hostlen != 0) || idx == iAfterHost
		|| (idx == iPort && url->portlen != 0)
		|| idx == iPath || idx == iQs);
	if (consistent)
		er = FFURL_EOK;
	else
		er = FFURL_EMORE;

fin:
	url->idx = (byte)idx;
	url->len = (ushort)i;

	if (er != FFURL_EMORE && !(idx == iPath || idx == iQs))
		url->offpath = url->len;

	return er;
}

static const char *const serr[] = {
	"ok"
	, "more data"
	, "done"
	, "value too large"
	, "bad scheme"
	, "bad IPv6 address"
	, "bad host"
	, "bad path"
};

const char *ffurl_errstr(int er)
{
	return serr[er];
}

ffstr ffurl_get(const ffurl *url, const char *base, int comp)
{
	ffstr s = { 0, NULL };

	switch (comp) {
	case FFURL_FULLHOST:
		{
			size_t n = url->hostlen;
			if (url->portlen != 0)
				n += FFSLEN(":") + url->portlen;
			ffstr_set(&s, base + url->offhost, n);
		}
		break;

	case FFURL_SCHEME:
		{
			size_t n = (url->offhost != 0 ? url->offhost - FFSLEN("://") : 0);
			ffstr_set(&s, base + 0, n);
		}
		break;

	case FFURL_HOST:
		ffstr_set(&s, base + url->offhost, url->hostlen);
		// [::1] -> ::1
		if (url->hostlen > 2 && base[url->offhost] == '[') {
			s.ptr++;
			s.len -= 2;
		}
		break;

	case FFURL_PORT:
		ffstr_set(&s, base + url->offhost + url->hostlen + FFSLEN(":"), url->portlen);
		break;

	case FFURL_PATH:
		ffstr_set(&s, base + url->offpath, url->pathlen);
		break;

	case FFURL_QS:
		if (url->querystr) {
			size_t off = url->offpath + url->pathlen + FFSLEN("?");
			ffstr_set(&s, base + off, url->len - off);
		}
		break;

	case FFURL_PATHQS:
		ffstr_set(&s, base + url->offpath, url->len - url->offpath);
		break;
	}

	return s;
}

int ffurl_parse_ip(ffurl *u, const char *base, ffip6 *dst)
{
	ffstr s = ffurl_get(u, base, FFURL_HOST);
	if (u->ipv4) {
		if (0 != ffip4_parse((void*)dst, s.ptr, s.len))
			return -1;
		return AF_INET;

	} else if (u->ipv6) {
		if (0 != ffip6_parse((void*)dst, s.ptr, s.len))
			return -1;
		return AF_INET6;
	}

	return 0;
}

int ffip_parse(const char *ip, size_t len, ffip6 *dst)
{
	if (0 == ffip4_parse((void*)dst, ip, len))
		return AF_INET;
	if (0 == ffip6_parse((void*)dst, ip, len))
		return AF_INET6;
	return 0;
}

int ffurl_joinstr(ffstr *dst, const ffstr *scheme, const ffstr *host, uint port, const ffstr *path, const ffstr *querystr)
{
	if (scheme->len == 0 || host->len == 0
		|| (path->len != 0 && path->ptr[0] != '/'))
		return 0;

	ffarr a = {};
	if (NULL == ffarr_alloc(&a, scheme->len + FFSLEN("://:12345/?") + host->len + path->len + querystr->len))
		return 0;
	char *p = a.ptr;
	const char *end = ffarr_edge(&a);

	p += ffs_lower(p, end - p, scheme->ptr, scheme->len);
	p = ffs_copy(p, end, "://", 3);

	p += ffs_lower(p, end - p, host->ptr, host->len);

	if (port != 0) {
		FF_ASSERT(0 == (port & ~0xffff));
		p = ffs_copy(p, end, ":", 1);
		p += ffs_fromint(port & 0xffff, p, end - p, 0);
	}

	if (path->len == 0)
		p = ffs_copy(p, end, "/", 1);
	else
		p += ffpath_normalize(p, end - p, path->ptr, path->len, FFPATH_NO_DISK_LETTER | FFPATH_SLASH_BACKSLASH | FFPATH_FORCE_SLASH);

	if (querystr->len != 0) {
		p = ffs_copy(p, end, "?", 1);
		p = ffs_copy(p, end, querystr->ptr, querystr->len);
	}

	FF_ASSERT(a.cap >= (size_t)(p - (char*)a.ptr));
	ffstr_set(dst, a.ptr, p - (char*)a.ptr);
	return dst->len;
}


int ffip_next(ffip_iter *it, void **ip)
{
	if (it->list != NULL) {

		if (it->idx < it->list->ip4.len) {
			*ip = ffarr_itemT(&it->list->ip4, it->idx, ffip4);
			it->idx++;
			return AF_INET;
		}
		if (it->idx - it->list->ip4.len < it->list->ip6.len) {
			*ip = ffarr_itemT(&it->list->ip6, it->idx - it->list->ip4.len, ffip6);
			it->idx++;
			return AF_INET6;
		}
		it->list = NULL;
	}

	if (it->ai != NULL) {
		uint family = it->ai->ai_family;
		union {
			struct sockaddr_in *a;
			struct sockaddr_in6 *a6;
		} u;
		u.a = (void*)it->ai->ai_addr;
		*ip = (family == AF_INET) ? (void*)&u.a->sin_addr : (void*)&u.a6->sin6_addr;
		it->ai = it->ai->ai_next;
		return family;
	}

	return 0;
}


uint ffip_tostr(char *buf, size_t cap, uint family, const void *ip, uint port)
{
	char *end = buf + cap, *p = buf;
	uint n;

	if (family == AF_INET) {
		if (0 == (n = ffip4_tostr(ip, buf, cap)))
			return 0;
		p += n;

	} else {
		if (port != 0)
			p = ffs_copyc(p, end, '[');
		if (0 == (n = ffip6_tostr(ip, p, end - p)))
			return 0;
		p += n;
		if (port != 0)
			p = ffs_copyc(p, end, ']');
	}

	if (port != 0)
		p += ffs_fmt(p, end, ":%u", port);

	return p - buf;
}

