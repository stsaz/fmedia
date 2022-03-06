/**
Copyright (c) 2013 Simon Zolin
*/

#include "string.h"
#include <FFOS/error.h>
#include <math.h>

char * ffs_rskip(const char *buf, size_t len, int ch)
{
	const char *end = buf + len;
	while (end != buf && *(end - 1) == ch)
		end--;
	return (char*)end;
}

char* ffs_skip_mask(const char *buf, size_t len, const uint *mask)
{
	for (size_t i = 0;  i != len;  i++) {
		if (!ffbit_testarr(mask, (byte)buf[i]))
			return (char*)buf + i;
	}
	return (char*)buf + len;
}


static const int64 ff_intmasks[9] = {
	0
	, 0xff, 0xffff, 0xffffff, 0xffffffff
	, 0xffffffffffULL, 0xffffffffffffULL, 0xffffffffffffffULL, 0xffffffffffffffffULL
};

ssize_t ffs_findarr(const void *ar, size_t n, uint elsz, const void *s, size_t len)
{
	if (len <= sizeof(int)) {
		int imask = ff_intmasks[len];
		int left = *(int*)s & imask;
		for (size_t i = 0;  i != n;  i++) {
			if (left == (*(int*)ar & imask) && ((byte*)ar)[len] == 0x00)
				return i;
			ar = (byte*)ar + elsz;
		}
	} else if (len <= sizeof(int64)) {
		int64 left;
		size_t i;
		int64 imask;
		imask = ff_intmasks[len];
		left = *(int64*)s & imask;
		for (i = 0;  i != n;  i++) {
			if (left == (*(int64*)ar & imask) && ((byte*)ar)[len] == 0x00)
				return i;
			ar = (byte*)ar + elsz;
		}
	}
	return -1;
}

const uint ffcharmask_name[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0x03ff0000, // 0000 0011 1111 1111  0000 0000 0000 0000
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0x87fffffe, // 1000 0111 1111 1111  1111 1111 1111 1110
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x07fffffe, // 0000 0111 1111 1111  1111 1111 1111 1110
	0,
	0,
	0,
	0
};

const uint ffcharmask_nowhite[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xfffffffe, // 1111 1111 1111 1111  1111 1111 1111 1110
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0,
	0,
	0,
	0
};

const uint ffcharmask_printable[] = {
	0,
	            // ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            // _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@
	0xffffffff, // 1111 1111 1111 1111  1111 1111 1111 1111
	            //  ~}| {zyx wvut srqp  onml kjih gfed cba`
	0x7fffffff, // 0111 1111 1111 1111  1111 1111 1111 1111
	0xffffffff,
	0xffffffff,
	0xffffffff,
	0xffffffff
};


size_t ffs_fmatchv(const char *s, size_t len, const char *fmt, va_list va)
{
	const char *s_o = s, *s_end = s + len;
	uint width, iflags = 0;
	union {
		char *s;
		ffstr *str;
		uint *i4;
		uint64 *i8;
	} dst;
	dst.s = NULL;

	for (;  s != s_end && *fmt != '\0';  s++) {

		if (*fmt != '%') {
			if (*fmt != *s)
				goto fail; //mismatch
			fmt++;
			continue;
		}

		fmt++; //skip %

		if (*fmt == '%') {
			if (*fmt != *s)
				goto fail; //mismatch
			fmt++;
			continue;
		}

		width = 0;
		while (ffchar_isdigit(*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}

		switch (*fmt) {
		case 'x':
			iflags |= FFS_INTHEX;
			fmt++;
			break;
		}

		switch (*fmt) {
		case 'U':
			dst.i8 = va_arg(va, uint64*);
			iflags |= FFS_INT64;
			break;

		case 'u':
			dst.i4 = va_arg(va, uint*);
			iflags |= FFS_INT32;
			break;

		case 's':
			if (iflags != 0)
				goto fail; //unsupported modifier
			dst.s = va_arg(va, char*);
			if (width == 0)
				goto fail; //width must be specified for %s
			ffmemcpy(dst.s, s, width);
			s += width;
			break;

		case 'S':
			if (iflags != 0)
				goto fail; //unsupported modifier
			dst.str = va_arg(va, ffstr*);
			dst.str->ptr = (void*)s;

			if (width != 0) {
				dst.str->len = width;
				s += width;

			} else {

				for (;  s != s_end;  s++) {
					if (!ffchar_isletter(*s))
						break;
				}
				dst.str->len = s - dst.str->ptr;
			}
			break;

		default:
			goto fail; //invalid format specifier
		}

		if (iflags != 0) {
			size_t n = (width == 0) ? (size_t)(s_end - s) : ffmin(s_end - s, width);
			n = ffs_toint(s, n, dst.i8, iflags);
			if (n == 0)
				goto fail; //bad integer
			s += n;
			iflags = 0;
		}

		fmt++;
		s--;
	}

	if (*fmt != '\0')
		goto fail; //input string is too short

	return s - s_o;

fail:
	return -(s - s_o + 1);
}

size_t ffstr_nextval(const char *buf, size_t len, ffstr *dst, int spl)
{
	const char *end = buf + len;
	const char *pos;
	ffstr spc, sspl = {0};
	uint f = spl & ~0xff;
	spl &= 0xff;

	ffstr_setcz(&spc, " ");
	if (f & FFS_NV_CR)
		ffstr_setcz(&spc, " \t\r");

	if (buf == end) {
		dst->len = 0;
		return len;
	}

	if (sspl.ptr != NULL)
		pos = ffs_findof(buf, end - buf, sspl.ptr, sspl.len);
	else
		pos = ffs_find(buf, end - buf, spl);

	if (pos != end) {
		len = pos - (end - len) + 1;
		if (pos + 1 == end && pos != buf)
			len--; // don't remove the last split char, e.g. "val,"
	}

	ffstr_set(dst, buf, pos - buf);
	return len;
}
