/** ffbase: conf writer
2021, Simon Zolin
*/

/*
ffconfw_init ffconfw_close
ffconfw_addkey ffconfw_addkeyz
ffconfw_addstr ffconfw_addstrz
ffconfw_addpair ffconfw_addpairz
ffconfw_addint ffconfw_addintf
ffconfw_addfloat
ffconfw_addline ffconfw_addlinez
ffconfw_addobj
ffconfw_add
ffconfw_fin
ffconfw_output
ffconfw_clear
*/

#pragma once
#include "conf2-scheme.h"

typedef struct ffconfw {
	ffvec buf;
	ffuint flags;
} ffconfw;

enum FFCONFW_FLAGS {
	/** Don't escape strings */
	FFCONFW_FDONTESCAPE = 1<<30,
	/** Add value as is, no quotes */
	FFCONFW_FLINE = 1<<29,
	/** Use CRLF instead of LF */
	FFCONFW_FCRLF = 1<<28,
};

/** Initialize writer
flags: enum FFCONFW_FLAGS */
static inline void ffconfw_init(ffconfw *c, ffuint flags)
{
	ffmem_zero_obj(c);
	c->flags = flags;
}

/** Close writer */
static inline void ffconfw_close(ffconfw *c)
{
	ffvec_free(&c->buf);
}

static int ffconfw_add(ffconfw *c, ffuint t, const void *src);

/** Add string */
static inline int ffconfw_addline(ffconfw *c, const ffstr *s)
{
	return ffconfw_add(c, (1<<31) | FFCONFW_FLINE | FFCONFW_FDONTESCAPE, s);
}

/** Add string */
static inline int ffconfw_addlinez(ffconfw *c, const char *sz)
{
	ffstr s;
	ffstr_setz(&s, sz);
	return ffconfw_add(c, (1<<31) | FFCONFW_FLINE | FFCONFW_FDONTESCAPE, &s);
}

/** Add string */
static inline int ffconfw_addstr(ffconfw *c, const ffstr *s)
{
	return ffconfw_add(c, FFCONF_TSTR, s);
}

/** Add NULL-terminated string */
static inline int ffconfw_addstrz(ffconfw *c, const char *sz)
{
	ffstr s;
	ffstr_setz(&s, sz);
	return ffconfw_add(c, FFCONF_TSTR, &s);
}

/** Add NULL-terminated string */
static inline int ffconfw_addkey(ffconfw *c, const ffstr *str)
{
	return ffconfw_add(c, 1<<31, str);
}

/** Add NULL-terminated string */
static inline int ffconfw_addkeyz(ffconfw *c, const char *sz)
{
	ffstr s;
	ffstr_setz(&s, sz);
	return ffconfw_add(c, 1<<31, &s);
}

/** Add key and value */
static inline int ffconfw_addpair(ffconfw *c, const ffstr *key, const ffstr *val)
{
	int r;
	if ((r = ffconfw_add(c, 1<<31, key)) < 0)
		return r;
	int n = r;
	if ((r = ffconfw_add(c, FFCONF_TSTR, val)) < 0)
		return r;
	return n + r;
}

/** Add key and value as NULL-terminated string */
static inline int ffconfw_addpairz(ffconfw *c, const char *key, const char *val)
{
	ffstr k, v;
	ffstr_setz(&k, key);
	ffstr_setz(&v, val);
	return ffconfw_addpair(c, &k, &v);
}

/** Add integer */
static inline int ffconfw_addintf(ffconfw *c, ffint64 val, ffuint int_flags)
{
	char buf[64];
	int r = ffs_fromint(val, buf, sizeof(buf), FFS_INTSIGN | int_flags);
	ffstr s;
	ffstr_set(&s, buf, r);
	return ffconfw_add(c, FFCONF_TSTR, &s);
}

/** Add integer */
static inline int ffconfw_addint(ffconfw *c, ffint64 val)
{
	return ffconfw_addintf(c, val, 0);
}

/**
float_flags: precision | enum FFS_FROMFLOAT | FFS_FLTWIDTH() */
static inline ffsize ffconfw_addfloat(ffconfw *c, double val, ffuint float_flags)
{
	char buf[64];
	uint n = ffs_fromfloat(val, buf, sizeof(buf), float_flags);
	ffstr s;
	ffstr_set(&s, buf, n);
	return ffconfw_add(c, FFCONF_TSTR, &s);
}

/** Add object */
static inline int ffconfw_addobj(ffconfw *c, ffuint open)
{
	return ffconfw_add(c, FFCONF_TOBJ | (open ? 1<<31 : 0), NULL);
}

static ffsize ffconf_escape(char *dst, ffsize cap, const char *s, ffsize len)
{
	static const char esc_btoch[256] = {
		1,1,1,1,1,1,1,1,'b','t','n',1,'f','r',1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
		0,0,'"',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'\\',0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
		0,//...
	};
	ffsize n = 0;

	if (dst == NULL) {
		for (ffsize i = 0;  i < len;  i++) {
			ffuint ch = esc_btoch[(ffbyte)s[i]];
			if (ch == 0)
				n++;
			else if (ch == 1)
				n += FFS_LEN("\\x??");
			else
				n += FFS_LEN("\\?");
		}
		return n;
	}

	for (ffsize i = 0;  i < len;  i++) {
		ffuint ch = esc_btoch[(ffbyte)s[i]];

		if (ch == 0) {
			dst[n++] = s[i];

		} else if (ch == 1) {
			if (n + FFS_LEN("\\x??") > cap)
				return 0;
			dst[n++] = '\\';
			dst[n++] = 'x';
			dst[n++] = ffHEX[(ffbyte)s[i] >> 4];
			dst[n++] = ffHEX[(ffbyte)s[i] & 0x0f];

		} else {
			if (n + FFS_LEN("\\?") > cap)
				return 0;

			dst[n++] = '\\';
			dst[n++] = ch;
		}
	}

	return n;
}

static inline int ffconfw_size(ffconfw *c, ffuint type_flags, const void *src, int *complex)
{
	ffsize cap = 0;
	*complex = 0;
	switch (type_flags & 0x8000000f) {
	case FFCONF_TSTR:
	case 1<<31: {
		const ffstr *s = (ffstr*)src;
		ffsize r = s->len;
		if (!(type_flags & FFCONFW_FDONTESCAPE))
			r = ffconf_escape(NULL, 0, s->ptr, s->len);
		const char *MUST_QUOTE = " {}#/";
		if (r != s->len || s->len == 0 || ffstr_findanyz(s, MUST_QUOTE) >= 0)
			*complex = 1;
		cap = FFS_LEN("\r\n\"\"") + r;
		break;
	}
	case _FFCONF_TINT:
		cap = 1 + FFS_INTCAP;
		break;
	case FFCONF_TOBJ | (1<<31):
	case FFCONF_TOBJ:
		cap = FFS_LEN("\r\n}");
		break;
	}
	return cap;
}

/** Add 1 JSON element
Reallocate buffer by twice the size
type_flags: enum FFCONFW_FLAGS
src: ffint64 | ffstr
Return N of bytes written;
 <0 on error */
static inline int ffconfw_add(ffconfw *c, ffuint type_flags, const void *src)
{
	int t = type_flags & 0x8000000f;
	int complex;
	type_flags |= c->flags;
	ffsize r = ffconfw_size(c, type_flags, src, &complex);
	if (NULL == ffvec_growtwiceT(&c->buf, r, char))
		return -FFCONF_ESYS;
	ffsize oldlen = c->buf.len;

	switch (t) {
	case FFCONF_TSTR:
	case 1<<31: {
		const ffstr *s = (ffstr*)src;

		if (t == FFCONF_TSTR) {
			*ffstr_push(&c->buf) = ' ';
		} else if (c->buf.len != 0) {
			if (type_flags & FFCONFW_FCRLF)
				*ffstr_push(&c->buf) = '\r';
			*ffstr_push(&c->buf) = '\n';
		}

		if (complex && !(type_flags & FFCONFW_FLINE)) {
			*ffstr_push(&c->buf) = '\"';
			if (!(type_flags & FFCONFW_FDONTESCAPE))
				c->buf.len += ffconf_escape(ffstr_end(&c->buf), ffvec_unused(&c->buf), s->ptr, s->len);
			else
				ffstr_add((ffstr*)&c->buf, c->buf.cap, s->ptr, s->len);
			*ffstr_push(&c->buf) = '\"';
		} else {
			ffstr_add((ffstr*)&c->buf, c->buf.cap, s->ptr, s->len);
		}
		break;
	}

	case _FFCONF_TINT: {
		ffint64 i = *(ffint64*)src;
		*ffstr_push(&c->buf) = ' ';
		c->buf.len += ffs_fromint(i, ffstr_end(&c->buf), ffvec_unused(&c->buf), FFS_INTSIGN);
		break;
	}

	case FFCONF_TOBJ | (1<<31):
		*ffstr_push(&c->buf) = ' ';
		*ffstr_push(&c->buf) = '{';
		break;
	case FFCONF_TOBJ:
		if (type_flags & FFCONFW_FCRLF)
			*ffstr_push(&c->buf) = '\r';
		*ffstr_push(&c->buf) = '\n';
		*ffstr_push(&c->buf) = '}';
		break;
	}

	return c->buf.len - oldlen;
}

static inline int ffconfw_fin(ffconfw *c)
{
	ffvec_addchar(&c->buf, '\n');
	return 0;
}

/** Get output data */
static inline void ffconfw_output(ffconfw *c, ffstr *out)
{
	ffstr_setstr(out, &c->buf);
}

#define ffconfw_clear(c)  ((c)->buf.len = 0)
