/** ff: command-line arguments parser with scheme
2020, Simon Zolin
*/

/*
ffcmdarg_scheme_init
ffcmdarg_scheme_process
ffcmdarg_parse_object
*/

#pragma once
#include "cmdarg.h"

enum FFCMDARG_SCHEME_T {
	FFCMDARG_TSWITCH,
	FFCMDARG_TSTR,
	FFCMDARG_TSTRZ,
	_FFCMDARG_TINT,
	_FFCMDARG_TFLOAT,

	FFCMDARG_F32BIT = 0x20,
	FFCMDARG_F16BIT = 0x40,
	FFCMDARG_F8BIT = 0x80,
	FFCMDARG_FMULTI = 0x0100,
	FFCMDARG_FNOTEMPTY = 0x0200,
	FFCMDARG_FSIGN = 0x0400,

	FFCMDARG_TINT64 = _FFCMDARG_TINT,
	FFCMDARG_TINT32 = _FFCMDARG_TINT | FFCMDARG_F32BIT,
	FFCMDARG_TINT16 = _FFCMDARG_TINT | FFCMDARG_F16BIT,
	FFCMDARG_TINT8 = _FFCMDARG_TINT | FFCMDARG_F8BIT,
	FFCMDARG_TFLOAT64 = _FFCMDARG_TFLOAT,
	FFCMDARG_TFLOAT32 = _FFCMDARG_TFLOAT | FFCMDARG_F32BIT,
};

typedef struct ffcmdarg_arg {
	/** Short name of the argument, e.g. 's' matches "-s"
	0: argument has no short name */
	char short_name;

	/** Long name of the argument, e.g. "long-name" matches "--long-name"
	"": this entry matches any stand-alone value.  Must be the first. */
	const char *long_name;

	/** Flags
	FFCMDARG_TSTR:
	  * offset to ffstr field; user must free it with ffstr_free()
	  * int handler(ffcmdarg_scheme *as, void *obj, ffstr *s)
	      where 's' data is transient

	FFCMDARG_TSTRZ:
	  * offset to 'char*' field; user must free it with ffmem_free()
	  * int handler(ffcmdarg_scheme *as, void *obj, char *s)
	      where 's' data is transient

	FFCMDARG_TINT??:
	  * offset to ffint64|int|short|ffbyte field
	  * int handler(ffcmdarg_scheme *as, void *obj, ffint64 i)

	FFCMDARG_TFLOAT??:
	  * offset to double|float field
	  * int handler(ffcmdarg_scheme *as, void *obj, double d)

	FFCMDARG_TSWITCH:
	  * offset to ffbyte field
	  * int handler(ffcmdarg_scheme *as, void *obj)
	*/
	ffuint flags;

	/** Offset to a structure field or a function pointer.
	handler() returns 0 on success or enum FFCMDARG_E
	*/
	ffsize dst;
} ffcmdarg_arg;

typedef struct ffcmdarg_scheme {
	ffcmdarg *parser;
	ffuint flags; // enum FFCMDARG_SCHEME_F
	const ffcmdarg_arg *arg;
	const ffcmdarg_arg *args;
	void *obj;
	const char *errmsg;
	ffuint used_bits[2];
} ffcmdarg_scheme;

enum FFCMDARG_SCHEME_F {
	FFCMDARG_SCF_DUMMY,
};

/** Initialize parser object
args: array of arguments; terminated by NULL entry
scheme_flags: enum FFCMDARG_SCHEME_F */
static inline void ffcmdarg_scheme_init(ffcmdarg_scheme *as, const ffcmdarg_arg *args, void *obj, ffcmdarg *a, ffuint scheme_flags)
{
	ffmem_zero_obj(as);
	as->flags = scheme_flags;
	as->parser = a;
	as->args = args;
	as->obj = obj;
}

/** Find element by a long name */
static inline const ffcmdarg_arg* _ffcmdarg_arg_find(const ffcmdarg_arg *args, ffstr name)
{
	for (ffuint i = 0;  args[i].long_name != NULL;  i++) {
		if (ffstr_eqz(&name, args[i].long_name)) {
			return &args[i];
		}
	}
	return NULL;
}

/** Find element by a short name */
static inline const ffcmdarg_arg* _ffcmdarg_arg_find_short(const ffcmdarg_arg *args, char short_name)
{
	for (ffuint i = 0;  args[i].long_name != NULL;  i++) {
		if (short_name == args[i].short_name) {
			return &args[i];
		}
	}
	return NULL;
}

#define _FFCMDARG_ERR(a, msg) \
	(a)->errmsg = msg,  -FFCMDARG_ESCHEME

/** Process 1 argument
Return 'r';
  <0 on error: enum FFCMDARG_E */
static inline int ffcmdarg_scheme_process(ffcmdarg_scheme *as, int r)
{
	if (r < 0)
		return r;

	const ffuint MAX_OFF = 64*1024;
	union {
		ffstr *s;
		char **sz;
		char *i8;
		short *i16;
		int *i32;
		ffint64 *i64;
		float *f32;
		double *f64;
		ffbyte *b;
		int (*func)(ffcmdarg_scheme *as, void *obj);
		int (*func_str)(ffcmdarg_scheme *as, void *obj, ffstr *s);
		int (*func_sz)(ffcmdarg_scheme *as, void *obj, char *sz);
		int (*func_int)(ffcmdarg_scheme *as, void *obj, ffint64 i);
		int (*func_float)(ffcmdarg_scheme *as, void *obj, double d);
	} u;
	u.s = NULL;

	int r2;
	ffuint type = 0, flags = 0;
	ffstr val = as->parser->val;

	switch (r) {
	case FFCMDARG_RKEYSHORT:
	case FFCMDARG_RKEYLONG: {
		if (as->arg != NULL)
			return _FFCMDARG_ERR(as, "expected value for the previous argument");

		if (r == FFCMDARG_RKEYSHORT)
			as->arg = _ffcmdarg_arg_find_short(as->args, val.ptr[0]);
		else
			as->arg = _ffcmdarg_arg_find(as->args, val);

		if (as->arg == NULL)
			return _FFCMDARG_ERR(as, "no such key in scheme");

		ffuint i = as->arg - as->args;
		if (i < sizeof(as->used_bits)*8
			&& ffbit_array_set(&as->used_bits, i)
			&& !(as->arg->flags & FFCMDARG_FMULTI))
			return _FFCMDARG_ERR(as, "duplicate key");

		type = as->arg->flags & 0x0f;
		u.b = (ffbyte*)as->arg->dst;
		if (as->arg->dst < MAX_OFF)
			u.b = (ffbyte*)FF_PTR(as->obj, as->arg->dst);

		if (type == FFCMDARG_TSWITCH) {
			if (as->arg->dst < MAX_OFF)
				*u.b = 1;
			else if (0 != (r2 = u.func(as, as->obj)))
				return -r2; // user error
			as->arg = NULL;
		}
		break;
	}

	case FFCMDARG_RVAL:
		if (as->arg == NULL
			&& as->args[0].long_name != NULL && as->args[0].long_name[0] == '\0')
			as->arg = &as->args[0];
		// fallthrough

	case FFCMDARG_RKEYVAL:
		if (as->arg == NULL)
			return _FFCMDARG_ERR(as, "unexpected value");

		type = as->arg->flags & 0x0f;
		flags = as->arg->flags;
		u.b = (ffbyte*)as->arg->dst;
		if (as->arg->dst < MAX_OFF)
			u.b = (ffbyte*)FF_PTR(as->obj, as->arg->dst);

		switch (type) {
		case FFCMDARG_TSTR:
			if ((flags & FFCMDARG_FNOTEMPTY) && val.len == 0)
				return _FFCMDARG_ERR(as, "value must not be empty");

			if (as->arg->dst < MAX_OFF) {
				ffstr_free(u.s);
				if (NULL == ffstr_dupstr(u.s, &val))
					return _FFCMDARG_ERR(as, "no memory");
			} else if (0 != (r2 = u.func_str(as, as->obj, &val)))
				return -r2; // user error
			break;

		case FFCMDARG_TSTRZ: {
			if ((flags & FFCMDARG_FNOTEMPTY) && val.len == 0)
				return _FFCMDARG_ERR(as, "value must not be empty");

			if (ffstr_findchar(&val, '\0') >= 0)
				return _FFCMDARG_ERR(as, "value must not contain NULL character");

			char *sz;
			if (NULL == (sz = ffsz_dupstr(&val)))
				return _FFCMDARG_ERR(as, "no memory");
			if (as->arg->dst < MAX_OFF) {
				ffmem_free(*u.sz);
				*u.sz = sz;
				sz = NULL;
			} else if (0 != (r2 = u.func_sz(as, as->obj, sz))) {
				ffmem_free(sz);
				return -r2; // user error
			}
			ffmem_free(sz);
			break;
		}

		case _FFCMDARG_TINT: {
			ffint64 i = 0;
			ffuint f = FFS_INT64;
			if (flags & FFCMDARG_F32BIT)
				f = FFS_INT32;
			else if (flags & FFCMDARG_F16BIT)
				f = FFS_INT16;
			else if (flags & FFCMDARG_F8BIT)
				f = FFS_INT8;

			if (flags & FFCMDARG_FSIGN)
				f |= FFS_INTSIGN;

			if (!ffstr_toint(&val, &i, f))
				return _FFCMDARG_ERR(as, "integer expected");

			if (as->arg->dst < MAX_OFF) {
				if (flags & FFCMDARG_F32BIT)
					*u.i32 = i;
				else if (flags & FFCMDARG_F16BIT)
					*u.i16 = i;
				else if (flags & FFCMDARG_F8BIT)
					*u.i8 = i;
				else
					*u.i64 = i;
			} else if (0 != (r2 = u.func_int(as, as->obj, i)))
				return -r2; // user error
			break;
		}

		case _FFCMDARG_TFLOAT: {
			double d;
			if (!ffstr_to_float(&val, &d))
				return _FFCMDARG_ERR(as, "float expected");

			if (d < 0 && !(flags & FFCMDARG_FSIGN))
				return _FFCMDARG_ERR(as, "unsigned float expected");

			if (as->arg->dst < MAX_OFF) {
				if (flags & FFCMDARG_F32BIT)
					*u.f32 = d;
				else
					*u.f64 = d;
			} else if (0 != (r2 = u.func_float(as, as->obj, d)))
				return -r2; // user error
			break;
		}

		case FFCMDARG_TSWITCH:
			return _FFCMDARG_ERR(as, "value is specified but the argument is a switch");

		default:
			return _FFCMDARG_ERR(as, "invalid scheme data");
		}

		as->arg = NULL;
		break;

	case FFCMDARG_DONE:
		if (as->arg != NULL)
			return _FFCMDARG_ERR(as, "value expected");
		break;

	default:
		FF_ASSERT(0);
		return _FFCMDARG_ERR(as, "bad return value from parser");
	}

	return r;
}

/** Convert command-line arguments to a C object
scheme_flags: enum FFCMDARG_SCHEME_F
errmsg: (optional) error message; must free with ffstr_free()
Return 0 on success
  <0 on error: enum FFCMDARG_E */
static inline int ffcmdarg_parse_object(const ffcmdarg_arg *args, void *obj, const char **argv, ffuint argc, ffuint scheme_flags, ffstr *errmsg)
{
	int r;
	ffcmdarg a;
	ffcmdarg_init(&a, argv, argc);

	ffcmdarg_scheme as;
	ffcmdarg_scheme_init(&as, args, obj, &a, scheme_flags);

	for (;;) {
		ffstr val;
		r = ffcmdarg_parse(&a, &val);
		if (r < 0)
			break;

		r = ffcmdarg_scheme_process(&as, r);
		if (r <= 0)
			break;
	}

	if (r != 0 && errmsg != NULL) {
		ffsize cap = 0;
		const char *err = ffcmdarg_errstr(r);
		if (r == -FFCMDARG_ESCHEME)
			err = as.errmsg;
		ffstr_growfmt(errmsg, &cap, "near '%S': %s"
			, &a.val, err);
	}

	return r;
}

#undef _FFCMDARG_ERR
