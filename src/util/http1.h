/** Read/write HTTP/1 data
2022, Simon Zolin
*/

/*
http_req_parse http_req_write
http_resp_parse http_resp_write
http_hdr_parse http_hdr_write
httpchunked_parse httpchunked_write
httpurl_escape httpurl_unescape
httpurl_split
*/

/*
Request:
	METHOD URL VERSION CRLF
	[(NAME:VALUE CRLF)...]
	CRLF
	[BODY]

Response:
	VERSION CODE MSG CRLF
	[(NAME:VALUE CRLF)...]
	CRLF
	[BODY]

HTTP/1.1:
	. requires "Host" header field in request
	. assumes "Connection: keep-alive" by default
	. supports "Transfer-Encoding: chunked"

Chunked data example:
	a  CRLF
	datadatada CRLF
	0  CRLF
	CRLF

Example HTTP request via HTTP proxy:
	GET http://HOST/ HTTP/1.1
	Host: HOST

Example HTTPS request via HTTP proxy:
	CONNECT HOST:443 HTTP/1.1
	Host: HOST:443
*/

#pragma once
#include <ffbase/string.h>

static int httpurl_escape(char *buf, ffsize cap, ffstr url);

/** Parse HTTP request line, e.g. "GET /file HTTP/1.1\r\n"
Format:
  Method  URL    Proto
  (A-Z)+ +(\w)+ +(HTTP\/1\.\d) +\r?\n"
Return N of bytes processed
 =0 if need more data
 <0 on error */
static inline int http_req_parse(ffstr req, ffstr *method, ffstr *path, ffstr *proto)
{
	const char *d = req.ptr, *end = req.ptr + req.len;

	int r = ffs_skip_ranges(d, end - d, "\x41\x5a", 2); // "A-Z"
	if (r < 0)
		return 0;
	if (r == 0 || d[r] != ' ')
		return -1;
	ffstr_set(method, d, r);
	d += r+1;

	for (;;) {
		if (d == end)
			return 0;
		if (*d != ' ')
			break;
		d++;
	}

	r = ffs_skip_ranges(d, end - d, "\x21\x7e", 2); // printable ANSI
	if (r < 0)
		return 0;
	if (r == 0 || d[r] != ' ')
		return -1;
	ffstr_set(path, d, r);
	d += r+1;

	for (;;) {
		if (d == end)
			return 0;
		if (*d != ' ')
			break;
		d++;
	}

	if (d+8 <= end
		&& (ffint_be_cpu64(*(ffuint64*)d) & ~1ULL) != 0x485454502f312e30) // "HTTP/1.0|1"
		return -1;
	r = ffs_skip_ranges(d, end - d, "\x21\x7e", 2); // printable ANSI
	if (r < 0) {
		if (d+8 < end)
			return -1;
		return 0;
	} else if (r < 8) {
		return -1;
	}
	ffstr_set(proto, d, r);
	d += r;

	while (*d == ' ') {
		d++;
		if (d == end)
			return 0;
	}

	if (*d == '\r') {
		d++;
		if (d == end)
			return 0;
	}
	if (*d != '\n')
		return -1;
	d++;

	return d - req.ptr;
}

/** Parse HTTP response line, e.g. "HTTP/1.1 200 OK\r\n"
Notes:
 . the parts must be divided by 1 space character
 . code: integer, exactly 3 characters
Return N of bytes processed
 =0 if need more data
 <0 on error */
static inline int http_resp_parse(ffstr resp, ffstr *proto, ffuint *code, ffstr *msg)
{
	const char *d = resp.ptr, *end = resp.ptr + resp.len;

	int r = ffs_skip_ranges(d, end - d, "\x21\x7e", 2); // printable ANSI
	if (r < 0)
		return 0;
	if (r == 0 || d[r] != ' ')
		return -1;
	ffstr_set(proto, d, r);
	d += r+1;

	r = ffs_skip_ranges(d, end - d, "\x30\x39", 2); // "0-9"
	if (r < 0)
		return 0;
	else if (r != 3 || d[r] != ' ')
		return -1;
	*code = (d[0] - '0') * 100 + (d[1] - '0') * 10 + d[2] - '0';
	d += 4;

	r = ffs_skip_ranges(d, end - d, "\x20\x7e", 2); // basic latin
	if (r < 0)
		return 0;
	ffstr_set(msg, d, r);
	d += r;

	if (*d == '\r') {
		if (d+1 == end)
			return 0;
		d++;
	}
	if (*d != '\n')
		return -1;
	d++;

	return d - resp.ptr;
}

/** Parse HTTP header pair, e.g. "Key: Value\r\n"
Format:
  (-0-9A-Za-z)+: *(\w)* *\r\n
name: [output] field name, undefined on last CRLF
value: [output] field value, undefined on last CRLF
Return N of bytes processed
 =0 if need more data
 <0 on error */
static inline int http_hdr_parse(ffstr data, ffstr *name, ffstr *value)
{
	const char *d = data.ptr, *end = data.ptr+data.len;

	int r = ffs_skip_ranges(d, end - d, "\x2d\x2d\x30\x39\x41\x5a\x61\x7a", 8); // "-0-9A-Za-z"
	if (r < 0)
		return 0;
	else if (r == 0)
		goto crlf;
	else if (d[r] != ':' || *d == '-')
		return -1;
	ffstr_set(name, d, r);
	d += r+1;

	for (;;) {
		if (d == end)
			return 0;
		if (*d != ' ')
			break;
		d++;
	}

	r = ffs_findany(d, end - d, "\r\n", 2);
	if (r < 0)
		return 0;
	ffstr_set(value, d, r);
	d += r;

	while (value->len != 0 && value->ptr[value->len-1] == ' ') {
		value->len--;
	}

crlf:
	if (*d == '\r') {
		d++;
		if (d == end)
			return 0;
	}
	if (*d != '\n')
		return -1;
	d++;

	return d - data.ptr;
}


/** Write HTTP request line, e.g. "GET /path HTTP/1.1\r\n"
Return N of bytes written
 <0 if not enough space */
static inline int http_req_write(char *buf, ffsize cap, ffstr method, ffstr path, ffuint flags)
{
	ffuint n = method.len + path.len + 8+4;
	if (n > cap)
		return -1;

	char *p = buf;
	p = ffmem_copy(p, method.ptr, method.len);
	*p++ = ' ';
	if (flags == 0) {
		p = ffmem_copy(p, path.ptr, path.len);
	} else {
		ffssize r = httpurl_escape(p, buf+cap - p - (1+8+2), path);
		if (r < 0)
			return -1;
		p += r;
	}
	*p++ = ' ';
	ffmem_copy(p, "HTTP/1.1\r\n", 8+2);
	return n;
}

/** Write HTTP response line, e.g. "HTTP/1.1 200 OK\r\n"
Return N of bytes written
 <0 if not enough space */
static inline int http_resp_write(char *buf, ffsize cap, ffuint code, ffstr msg)
{
	ffuint n = 8+3 + msg.len + 4;
	if (n > cap)
		return -1;

	char *p = buf;
	p = ffmem_copy(p, "HTTP/1.1", 8);
	*p++ = ' ';

	*p++ = code/100 + '0';
	*p++ = (code%100)/10 + '0';
	*p++ = code%10 + '0';
	*p++ = ' ';

	p = ffmem_copy(p, msg.ptr, msg.len);
	*p++ = '\r';
	*p = '\n';
	return n;
}

/** Write HTTP header line, e.g. "Key: Value\r\n"
Return N of bytes written
 0 if not enough space */
static inline int http_hdr_write(char *buf, ffsize cap, ffstr name, ffstr val)
{
	ffuint n = name.len + val.len + 4;
	if (n > cap)
		return (buf != NULL) ? 0 : n;

	char *p = buf;
	p = ffmem_copy(p, name.ptr, name.len);
	*p++ = ':';
	*p++ = ' ';
	p = ffmem_copy(p, val.ptr, val.len);
	*p++ = '\r';
	*p = '\n';
	return n;
}


struct httpchunked {
	ffuint state;
	ffuint last_chunk;
	ffuint64 size;
};

/** Parse chunked data
Return N of bytes processed, `output` contains unchunked data (if any)
 -1 if done
 <0 on error */
static inline ffssize httpchunked_parse(struct httpchunked *c, ffstr input, ffstr *output)
{
	char *d = input.ptr;
	ffsize i, len = input.len;
	int st = c->state;
	enum { I_SZ1, I_SZ, I_SZ_CR, I_DAT, I_DAT_CR };
	output->len = 0;

	for (i = 0;  i != len;  i++) {
		int ch = d[i];

		switch (st) {
		case I_SZ1:
		case I_SZ: {
			int n = ffchar_tohex(ch);
			if (n < 0) {
				if (st == I_SZ1)
					return -2;

				if (ch == '\r')
					st = I_SZ_CR;
				else if (ch == '\n')
					st = I_DAT;
				else
					return -2;

				if (c->size == 0)
					c->last_chunk = 1;
				continue;
			}
			if (c->size & 0xf000000000000000ULL)
				return -2;
			c->size = (c->size << 4) | n;
			st = I_SZ;
			break;
		}

		case I_SZ_CR:
			if (ch != '\n')
				return -2;
			st = I_DAT;
			break;

		case I_DAT: {
			if (c->size == 0) {
				if (ch == '\r') {
					st = I_DAT_CR;
				} else if (ch == '\n') {
					if (c->last_chunk) {
						i = -1;
						goto end;
					}
					st = I_SZ;
				} else {
					return -2;
				}
				continue;
			}
			ffsize n = ffmin64(c->size, len - i);
			ffstr_set(output, &d[i], n);
			i += n;
			c->size -= n;
			goto end;
		}

		case I_DAT_CR:
			if (ch != '\n')
				return -2;
			if (c->last_chunk) {
				i = -1;
				goto end;
			}
			st = I_SZ;
			break;
		}
	}

end:
	c->state = st;
	return i;
}


/** Prepare chunked data
buf: buffer of at least 18 bytes for header and trailer */
static inline void httpchunked_write(char *buf, ffsize data_len, ffstr *hdr, ffstr *trl)
{
	char *p = buf;
	p += ffs_fromint(data_len, p, 18, FFS_INTHEX);
	*p++ = '\r';
	*p++ = '\n';
	ffstr_set(hdr, buf, p - buf);
	ffstr_set(trl, p - 2, 2);
}


/** Escape URL string with "%XX"
Return N of bytes written
 <0 if not enough space */
static inline int httpurl_escape(char *buf, ffsize cap, ffstr url)
{
	char *d = url.ptr, *end = url.ptr + url.len, *p = buf, *ebuf = buf + cap;

	while (d != end) {
		ffssize r = ffs_skip_ranges(d, end - d, "\x21\x7e", 2);
		if (r < 0)
			r = end - d;
		if (r > ebuf - p)
			return -1;
		p = ffmem_copy(p, d, r);
		d += r;

		if (d == end)
			break;

		if (3 > ebuf - p)
			return -1;
		*p++ = '%';
		*p++ = ffHEX[(ffbyte)*d >> 4];
		*p++ = ffHEX[(ffbyte)*d & 0x0f];
		d++;
	}

	return p - buf;
}

/** Replace each "%XX" escape sequence in URL string with a byte value
Return N of bytes written
 <0 if not enough space */
static inline int httpurl_unescape(char *buf, ffsize cap, ffstr url)
{
	char *d = url.ptr, *end = url.ptr + url.len, *p = buf, *ebuf = buf + cap;

	if (buf == NULL) {
		while (d < end) {
			ffssize r = ffs_skip_ranges(d, end - d, "\x21\x24\x26\x7e\x80\xff", 6);
			if (r < 0) {
				r = end - d;
			} else {
				cap++;
				d += 3;
			}
			d += r;
			cap += r;
		}
		return cap;
	}

	while (d != end) {
		ffssize r = ffs_skip_ranges(d, end - d, "\x21\x24\x26\x7e\x80\xff", 6); // printable except '%'
		if (r < 0)
			r = end - d;
		if (r > ebuf - p)
			return -1;
		p = ffmem_copy(p, d, r);
		d += r;

		if (d == end)
			break;

		if (d+3 > end || d[0] != '%')
			return -1;
		int h = ffchar_tohex(d[1]);
		int l = ffchar_tohex(d[2]);
		if (h < 0 || l < 0)
			return -1;
		if (p == ebuf)
			return -1;
		*p++ = (h<<4) | l;
		d += 3;
	}

	return p - buf;
}


struct httpurl_parts {
	ffstr scheme, host, port, path, query, hash;
};

/** Split URL into parts and don't validate input at all.
[http://] host|ip4|\[ip6\] [:80] [/path [?query] [#hash]]
Return 0 */
static inline int httpurl_split(struct httpurl_parts *parts, ffstr url)
{
	const char *u = url.ptr, *end = url.ptr + url.len, *p;
	ffssize r;

	r = ffs_findany(u, end - u, ":[//", 4);
	if (r < 0) {
		ffstr_set(&parts->host, u, end - u);
		return 0;
	}
	p = u + r;

	if (p+3 < end && p[0] == ':' && p[1] == '/' && p[2] == '/') { // e.g. "http://"
		ffstr_set(&parts->scheme, u, p+3 - u);
		u = p+3;
		r = ffs_findany(u, end - u, ":[//", 4);
		if (r < 0) {
			ffstr_set(&parts->host, u, end - u);
			return 0;
		}
		p = u + r;
	}

	if (*p == '[') { // e.g. "[::1]"
		p = (char*)ffmem_findbyte(p, end - p, ']');
		if (p == NULL) {
			ffstr_set(&parts->host, u, end - u);
			return 0;
		}
		p++;
		if (p == end) {
			ffstr_set(&parts->host, u, end - u);
			return 0;
		}
	}

	ffstr_set(&parts->host, u, p - u);
	u = p;

	if (*p == ':') { // e.g. "...:8080"
		p = (char*)ffmem_findbyte(u, end - u, '/');
		if (p == NULL) {
			ffstr_set(&parts->port, u, end - u);
			return 0;
		}
	}

	ffstr_set(&parts->port, u, p - u);
	u = p;

	if (*p != '/')
		return -1;

	r = ffs_findany(u, end - u, "?#", 2);
	if (r < 0) {
		ffstr_set(&parts->path, u, end - u);
		return 0;
	}
	p = u + r;
	ffstr_set(&parts->path, u, p - u);
	u = p;

	if (*p == '?') {
		p = (char*)ffmem_findbyte(u, end - u, '#');
		if (p == NULL) {
			ffstr_set(&parts->query, u, end - u);
			return 0;
		}
		ffstr_set(&parts->query, u, p - u);
		u = p;
	}

	ffstr_set(&parts->hash, u, end - u);
	return 0;
}
