#pragma once
#include "ffos-compat/types.h"
#include <FFOS/string.h>
#include <FFOS/error.h>
#include <ffbase/string.h>
#include <ffbase/stringz.h>

#if defined FF_UNIX
#include <stdarg.h> // for va_arg
#endif

#define FFSLEN(s)  FFS_LEN(s)

/** a-zA-Z0-9_ */
FF_EXTERN const uint ffcharmask_name[8];

/** non-whitespace ANSI */
FF_EXTERN const uint ffcharmask_nowhite[8];

/** All printable */
FF_EXTERN const uint ffcharmask_printable[8];

#define ffchar_lower(ch)  ((ch) | 0x20)

static inline ffbool ffchar_isdigit(int ch) {
	return (ch >= '0' && ch <= '9');
}

static inline ffbool ffchar_isup(int ch) {
	return (ch >= 'A' && ch <= 'Z');
}

static inline ffbool ffchar_islow(int ch) {
	return (ch >= 'a' && ch <= 'z');
}

static inline ffbool ffchar_ishex(int ch) {
	uint b = ffchar_lower(ch);
	return ffchar_isdigit(ch) || (b >= 'a' && b <= 'f');
}

static inline ffbool ffbit_testarr(const uint *ar, uint bit)
{
	return ffbit_test32(&ar[bit / 32], bit % 32);
}

#define ffchar_isletter(ch)  ffchar_islow(ffchar_lower(ch))

#define ffchar_isname(ch)  (0 != ffbit_testarr(ffcharmask_name, (byte)(ch)))

#define ffchar_isansiwhite(ch)  (0 == ffbit_testarr(ffcharmask_nowhite, (byte)(ch)))


#define ffs_cmp(s1, s2, n)  memcmp(s1, s2, n)

/** Return TRUE if a buffer and a constant NULL-terminated string are equal. */
#define ffs_eqcz(s1, len, csz2) \
	((len) == FFSLEN(csz2) && 0 == ffs_cmp(s1, csz2, len))


#ifndef FF_MSVC
#define ffsz_icmp(sz1, sz2)  strcasecmp(sz1, sz2)
#else
#define ffsz_icmp(sz1, sz2)  _stricmp(sz1, sz2)
#endif

/** Return NULL if not found. */
#define ffsz_findc(sz, ch)  strchr(sz, ch)

/** Search byte in a buffer.
Return END if not found. */
static inline char * ffs_find(const char *buf, size_t len, int ch) {
	char *pos = (char*)memchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}

/** Search byte in a buffer.
Return NULL if not found. */
#define ffs_findc(buf, len, ch)  memchr(buf, ch, len)

static inline char * ffs_findof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	ffssize r = ffs_findany(buf, len, anyof, cnt);
	if (r < 0)
		return (char*)buf + len;
	return (char*)buf + r;
}

static inline char * ffs_ifinds(const char *s, size_t len, const char *search, size_t search_len)
{
	ffssize i = ffs_ifindstr(s, len, search, search_len);
	if (i < 0)
		return (char*)s + len;
	return (char*)s + i;
}

static inline char* ffs_rfindof(const char *buf, size_t len, const char *anyof, size_t cnt)
{
	ffssize r = ffs_rfindany(buf, len, anyof, cnt);
	if (r < 0)
		return (char*)buf + len;
	return (char*)buf + r;
}


/** Split string by a character.
If split-character isn't found, the second string will be empty.
@first, @second: optional
@at: pointer within the range [s..s+len] or NULL.
Return @at or NULL. */
static inline const char* ffs_split2(const char *s, size_t len, const char *at, ffstr *first, ffstr *second)
{
	if (at == s + len)
		at = NULL;
	ffssize i = (at != NULL) ? at - s : -1;
	ffs_split(s, len, i, first, second);
	return at;
}

#define ffs_split2by(s, len, by, first, second) \
	ffs_split2(s, len, ffs_find(s, len, by), first, second)

enum FFSTR_NEXTVAL {
	FFS_NV_CR = 0x2000, // treat spaces, tabs, CR as whitespace
};

/** Get the next value from input string like "val1, val2, ...".
Spaces on the edges are trimmed.
@spl: split-character OR-ed with enum FFSTR_NEXTVAL.
Return the number of processed bytes. */
FF_EXTERN size_t ffstr_nextval(const char *buf, size_t len, ffstr *dst, int spl);

static inline size_t ffstr_nextval3(ffstr *src, ffstr *dst, int spl)
{
	size_t n = ffstr_nextval(src->ptr, src->len, dst, spl);
	ffstr_shift(src, n);
	return n;
}

/** Skip characters by mask.
@mask: uint[8] */
FF_EXTERN char* ffs_skip_mask(const char *buf, size_t len, const uint *mask);

/** Skip characters at the end of the string. */
FF_EXTERN char* ffs_rskip(const char *buf, size_t len, int ch);

/** Search a string in array using operations with type int64.
Return -1 if not found. */
FF_EXTERN ssize_t ffs_findarr(const void *ar, size_t n, uint elsz, const void *s, size_t len);
#define ffs_findarr3(ar, s, len)  ffs_findarr(ar, FFCNT(ar), sizeof(*ar), s, len)

#define ffs_findarrz(ar, n, search, search_len) \
	ffszarr_find(ar, n, search, search_len)

#define ffmemcpy  memcpy

/** Copy 1 character.
Return the tail. */
static inline char * ffs_copyc(char *dst, const char *bufend, int ch) {
	if (dst != bufend)
		*dst++ = (char)ch;
	return dst;
}

/** Copy buffer. */
static inline char * ffs_copy(char *dst, const char *bufend, const char *s, size_t len) {
	len = ffmin(bufend - dst, len);
	ffmemcpy(dst, s, len);
	return dst + len;
}

/** Copy buffer and append zero byte.
Return the pointer to the trailing zero. */
static inline char * ffsz_copy(char *dst, size_t cap, const char *src, size_t len) {
	char *end = dst + cap;
	if (cap != 0) {
		dst = ffs_copy(dst, end - 1, src, len);
		*dst = '\0';
	}
	return dst;
}

static inline char * ffsz_fcopy(char *dst, const char *src, size_t len) {
	ffmem_copy(dst, src, len);
	dst += len;
	*dst = '\0';
	return dst;
}

/** Allocate memory and copy string. */
static inline char* ffsz_alcopy(const char *src, size_t len)
{
	char *s = (char*)ffmem_alloc(len + 1);
	if (s != NULL)
		ffsz_fcopy(s, src, len);
	return s;
}

#define ffsz_alcopyz(src)  ffsz_alcopy(src, ffsz_len(src))
#define ffsz_alcopystr(src)  ffsz_alcopy((src)->ptr, (src)->len)

/**
Return the bytes copied. */
static inline size_t ffs_append(void *dst, size_t off, size_t cap, const void *src, size_t len)
{
	size_t n = ffmin(len, cap - off);
	ffmemcpy((char*)dst + off, src, n);
	return n;
}

static inline char* ffsz_alcopylwr(const char *src, size_t len)
{
	char *s = (char*)ffmem_alloc(len + 1);
	if (s == NULL)
		return NULL;
	ffs_lower(s, len, src, len);
	s[len] = '\0';
	return s;
}


#if defined FF_WIN
static inline char* ffsz_alcopyqz(const ffsyschar *wsz)
{
	size_t len = ffq_len(wsz) + 1, cap = ff_wtou(NULL, 0, wsz, len, 0);
	char *s = (char*)ffmem_alloc(cap);
	if (s != NULL)
		ff_wtou(s, cap, wsz, len, 0);
	return s;
}
#endif

enum { FFINT_MAXCHARS = FFSLEN("18446744073709551615") };

#define ffs_fmt2  ffs_format

static inline size_t ffs_fmt(char *buf, const char *end, const char *fmt, ...) {
	ssize_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_formatv(buf, end - buf, fmt, args);
	va_end(args);
	return (r >= 0) ? r : 0;
}

/** Match string by format:
 "% [width] x u|U" - uint|uint64
 "% width s" - char* (copy)
 "%S" - ffstr*
  "%S" - match letters only
  "% width S" - match "width" bytes
Return the number of bytes parsed.  Return negative value on error. */
FF_EXTERN size_t ffs_fmatchv(const char *s, size_t len, const char *fmt, va_list va);

static inline size_t ffs_fmatch(const char *s, size_t len, const char *fmt, ...) {
	size_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_fmatchv(s, len, fmt, args);
	va_end(args);
	return r;
}

/** Set array elements to point to consecutive regions of one buffer. */
static inline void ffarrp_setbuf(void **ar, size_t size, const void *buf, size_t region_len)
{
	size_t i;
	for (i = 0;  i != size;  i++) {
		ar[i] = (char*)buf + region_len * i;
	}
}
