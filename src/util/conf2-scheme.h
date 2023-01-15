/** ff: configuration parser with scheme
2020, Simon Zolin
*/

/*
ffconf_scheme_addctx ffconf_scheme_skipctx
ffconf_scheme_process
ffconf_scheme_keyname
ffconf_parse_object
ffconf_parse_file
*/

#pragma once
#include "conf2.h"
#include "conf2-ltconf.h"
#include <FFOS/file.h> // optional
#include <FFOS/error.h>
#include <ffbase/stringz.h>

enum FFCONF_SCHEME_T {
	FFCONF_TSTR = 1,
	FFCONF_TSTRZ,
	_FFCONF_TINT,
	_FFCONF_TSIZE, // number with multiplier suffix (e.g. 1k, 1m, 1g)
	_FFCONF_TFLOAT,
	_FFCONF_TBOOL, // 0 | 1 | true | false
	FFCONF_TOBJ,
	FFCONF_TCLOSE,

	/** Key may have more than 1 value: "key val1 val2..." */
	FFCONF_FLIST = 0x10,
	FFCONF_F32BIT = 0x20,
	FFCONF_F16BIT = 0x40,
	FFCONF_F8BIT = 0x80,
	/** Key may appear more than once */
	FFCONF_FMULTI = 0x0100,
	FFCONF_FNOTEMPTY = 0x0200,
	FFCONF_FSIGN = 0x0400,
	FFCONF_FNOTZERO = 0x0800,

	FFCONF_TINT64 = _FFCONF_TINT,
	FFCONF_TINT32 = _FFCONF_TINT | FFCONF_F32BIT,
	FFCONF_TINT16 = _FFCONF_TINT | FFCONF_F16BIT,
	FFCONF_TINT8 = _FFCONF_TINT | FFCONF_F8BIT,
	FFCONF_TSIZE64 = _FFCONF_TSIZE,
	FFCONF_TSIZE32 = _FFCONF_TSIZE | FFCONF_F32BIT,
	FFCONF_TSIZE16 = _FFCONF_TSIZE | FFCONF_F16BIT,
	FFCONF_TSIZE8 = _FFCONF_TSIZE | FFCONF_F8BIT,
	FFCONF_TFLOAT64 = _FFCONF_TFLOAT,
	FFCONF_TFLOAT32 = _FFCONF_TFLOAT | FFCONF_F32BIT,
	FFCONF_TBOOL8 = _FFCONF_TBOOL,
};

/** Maps CONF key name to a C struct field offset or a handler function */
typedef struct ffconf_arg {
	/** Key name
	"*": this entry matches any key. Must be the last.
	     ffconf_scheme_keyname() returns the key name. */
	const char *name;

	/**
	FFCONF_TSTR:
	  * offset to ffstr field; user must free it with ffstr_free()
	  * int handler(ffconf_scheme *cs, void *obj, ffstr *s)
	      where 's' data is transient.
	      User may use ffconf_strval_acquire() to acquire buffer from parser.

	FFCONF_TSTRZ:
	  * offset to 'char*' field
	    . user must free it with ffmem_free()
	    . previous value is erased with ffmem_free()
	  * int handler(ffconf_scheme *cs, void *obj, char *s)
	      where 's' data is transient.

	FFCONF_TINT??, FFCONF_TSIZE??:
	  * offset to ffint64|int|short|ffbyte field
	  * int handler(ffconf_scheme *cs, void *obj, ffint64 i)

	FFCONF_TBOOL8:
	  * offset to ffbyte field
	  * int handler(ffconf_scheme *cs, void *obj, ffint64 i)

	FFCONF_TOBJ:
	 int handler(ffconf_scheme *cs, void *obj)
	 User must call ffconf_scheme_addctx()

	FFCONF_TCLOSE:
	 int handler(ffconf_scheme *cs, void *obj)
	*/
	ffuint flags;

	/** Offset to a structure field or a function pointer.
	Offset is converted to a real pointer like this:
	 ptr = current_ctx.obj + offset
	handler() returns 0 on success or enum FFCONF_E
	*/
	ffsize dst;
} ffconf_arg;

/** Find element by name */
static inline const ffconf_arg* _ffconf_arg_find(const ffconf_arg *args, const ffstr *name)
{
	ffuint i;
	for (i = 0;  args[i].name != NULL;  i++) {
		if (ffstr_eqz(name, args[i].name)) {
			return &args[i];
		}
	}

	if (i != 0 && ffsz_eq(args[i-1].name, "*"))
		return &args[i-1];

	return NULL;
}

/** Find element by name (Case-insensitive) */
static inline const ffconf_arg* _ffconf_arg_ifind(const ffconf_arg *args, const ffstr *name)
{
	ffuint i;
	for (i = 0;  args[i].name != NULL;  i++) {
		if (ffstr_ieqz(name, args[i].name)) {
			return &args[i];
		}
	}

	if (i != 0 && ffsz_eq(args[i-1].name, "*"))
		return &args[i-1];

	return NULL;
}


struct ffconf_schemectx {
	const ffconf_arg *args;
	void *obj;
	ffuint used_bits[2];
};

typedef struct ffconf_scheme {
	ffconf *parser;
	ffuint flags; // enum FFCONF_SCHEME_F
	const ffconf_arg *arg;
	ffvec ctxs; // struct ffconf_schemectx[]
	ffstr any_keyname; // current key name for the "*" entry
	ffstr objval; // "key VALUE {"
	const char *errmsg;
} ffconf_scheme;

static inline void ffconf_scheme_init(ffconf_scheme *cs, ffconf *parser)
{
	ffmem_zero_obj(cs);
	cs->parser = parser;
}

static inline void ffconf_scheme_destroy(ffconf_scheme *cs)
{
	ffvec_free(&cs->ctxs);
	ffstr_free(&cs->any_keyname);
	ffstr_free(&cs->objval);
}

enum FFCONF_SCHEME_F {
	/** Case-insensitive key names */
	FFCONF_SCF_ICASE = 1,
};

/** Get the value preceding '{', e.g. "key value {" */
static inline ffstr* ffconf_scheme_objval(ffconf_scheme *cs)
{
	return &cs->objval;
}

/** Get the current key name for the "*" entry */
static inline ffstr* ffconf_scheme_keyname(ffconf_scheme *cs)
{
	return &cs->any_keyname;
}

/** Add new object context */
static inline void ffconf_scheme_addctx(ffconf_scheme *cs, const ffconf_arg *args, void *obj)
{
	struct ffconf_schemectx *c = ffvec_pushT(&cs->ctxs, struct ffconf_schemectx);
	c->args = args;
	c->obj = obj;
	ffmem_zero(c->used_bits, sizeof(c->used_bits));
}

/** Skip the object context being opened */
static inline void ffconf_scheme_skipctx(ffconf_scheme *cs)
{
	ffconf_scheme_addctx(cs, (ffconf_arg*)-1, (void*)-1);
}

/** Get bits to shift by size suffix K, M, G, T */
static ffuint _ffconf_sizesfx(int suffix)
{
	switch (suffix | 0x20) {
	case 'k':
		return 10;
	case 'm':
		return 20;
	case 'g':
		return 30;
	case 't':
		return 40;
	}
	return 0;
}

#define _FFCONF_ERR(c, msg) \
	(c)->errmsg = msg,  -FFCONF_ESCHEME

/** Process 1 element
r: parser's return code: enum FFCONF_R
Return 'r';
 <0 on error: enum FFCONF_E */
static inline int ffconf_scheme_process(ffconf_scheme *cs, int r)
{
	if (r <= 0)
		return r;

	const ffuint MAX_OFF = 64*1024;
	int r2;
	ffuint t = 0, flags = 0;
	ffstr *val = &cs->parser->val;
	struct ffconf_schemectx *ctx = ffslice_lastT(&cs->ctxs, struct ffconf_schemectx);
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
		int (*func)(ffconf_scheme *cs, void *obj);
		int (*func_str)(ffconf_scheme *cs, void *obj, ffstr *s);
		int (*func_sz)(ffconf_scheme *cs, void *obj, char *sz);
		int (*func_int)(ffconf_scheme *cs, void *obj, ffint64 i);
		int (*func_float)(ffconf_scheme *cs, void *obj, double d);
	} u;
	u.b = NULL;

	if (ctx->args == (void*)-1 && ctx->obj == (void*)-1) {
		// skip this object
		switch (r) {
		case FFCONF_ROBJ_OPEN:
			ffconf_scheme_skipctx(cs);
			break;
		case FFCONF_ROBJ_CLOSE:
			cs->ctxs.len--;
			break;
		}
		return r;
	}

	if (cs->arg != NULL) {
		t = cs->arg->flags & 0x0f;
		flags = cs->arg->flags;

		u.b = (ffbyte*)cs->arg->dst;
		if (cs->arg->dst < MAX_OFF)
			u.b = (ffbyte*)FF_PTR(ctx->obj, cs->arg->dst);
	}

	switch (r) {
	case FFCONF_ROBJ_OPEN: {
		if (t != FFCONF_TOBJ)
			return _FFCONF_ERR(cs, "got object, expected something else");

		ffsize nctx = cs->ctxs.len;
		if (0 != (r2 = u.func(cs, ctx->obj)))
			return -r2; // user error
		if (nctx + 1 != cs->ctxs.len)
			return _FFCONF_ERR(cs, "object handler must add a new context");
		ffstr_free(&cs->objval);
		cs->arg = NULL;
		break;
	}

	case FFCONF_ROBJ_CLOSE:
		for (ffuint i = 0;  ;  i++) {
			if (ctx->args[i].name == NULL) {
				cs->arg = &ctx->args[i];
				t = cs->arg->flags & 0x0f;
				if (t == FFCONF_TCLOSE) {
					u.b = (ffbyte*)cs->arg->dst;
					if (0 != (r2 = u.func(cs, ctx->obj)))
						return -r2; // user error
				}
				break;
			}
		}

		cs->ctxs.len--;
		cs->arg = NULL;
		break;

	case FFCONF_RKEY: {
		if (cs->flags & FFCONF_SCF_ICASE)
			cs->arg = _ffconf_arg_ifind(ctx->args, val);
		else
			cs->arg = _ffconf_arg_find(ctx->args, val);
		if (cs->arg == NULL)
			return _FFCONF_ERR(cs, "no such key in the current context");

		ffuint i = cs->arg - ctx->args;
		if (i < sizeof(ctx->used_bits)*8
			&& ffbit_array_set(&ctx->used_bits, i)
			&& !(cs->arg->flags & FFCONF_FMULTI))
			return _FFCONF_ERR(cs, "duplicate key");

		if (cs->arg->name[0] == '*' && cs->arg->name[1] == '\0') {
			ffstr_free(&cs->any_keyname);
			ffstr_dupstr(&cs->any_keyname, val);
		}
		break;
	}

	case FFCONF_RVAL_NEXT:
		if (!(flags & FFCONF_FLIST))
			return _FFCONF_ERR(cs, "the key doesn't expect multiple values");
		// fallthrough

	case FFCONF_RVAL:
		switch (t) {
		case FFCONF_TSTR:
			if ((flags & FFCONF_FNOTEMPTY) && val->len == 0)
				return _FFCONF_ERR(cs, "value must not be empty");

			if (cs->arg->dst < MAX_OFF) {
				ffstr_free(u.s);
				if (0 != ffconf_strval_acquire(cs->parser, u.s))
					return -FFCONF_ESYS;
			} else if (0 != (r2 = u.func_str(cs, ctx->obj, val))) {
				return -r2; // user error
			}
			break;

		case FFCONF_TSTRZ: {
			if ((flags & FFCONF_FNOTEMPTY) && val->len == 0)
				return _FFCONF_ERR(cs, "value must not be empty");

			if (ffstr_findchar(val, '\0') >= 0)
				return _FFCONF_ERR(cs, "value must not contain NULL character");

			char *sz;
			if (NULL == (sz = ffsz_dupstr(val)))
				return -FFCONF_ESYS;
			if (cs->arg->dst < MAX_OFF) {
				ffmem_free(*u.sz);
				*u.sz = sz;
				sz = NULL;
			} else if (0 != (r2 = u.func_sz(cs, ctx->obj, sz))) {
				ffmem_free(sz);
				return -r2; // user error
			}
			ffmem_free(sz);
			break;
		}

		case _FFCONF_TSIZE:
		case _FFCONF_TINT: {
			ffstr s = *val;
			ffuint shift = 0;

			if (t == _FFCONF_TSIZE && s.len >= 2) {
				shift = _ffconf_sizesfx(*ffslice_lastT(&s, char));
				if (shift != 0)
					s.len--;
			}

			ffint64 i = 0;
			ffuint f = FFS_INT64;
			if (flags & FFCONF_F32BIT)
				f = FFS_INT32;
			else if (flags & FFCONF_F16BIT)
				f = FFS_INT16;
			else if (flags & FFCONF_F8BIT)
				f = FFS_INT8;

			if (flags & FFCONF_FSIGN)
				f |= FFS_INTSIGN;

			if (!ffstr_toint(&s, &i, f))
				return _FFCONF_ERR(cs, "integer expected");
			i <<= shift;

			if (i == 0 && (flags & FFCONF_FNOTZERO))
				return _FFCONF_ERR(cs, "value must not be zero");

			if (cs->arg->dst < MAX_OFF) {
				if (flags & FFCONF_F32BIT)
					*u.i32 = i;
				else if (flags & FFCONF_F16BIT)
					*u.i16 = i;
				else if (flags & FFCONF_F8BIT)
					*u.i8 = i;
				else
					*u.i64 = i;
			} else if (0 != (r2 = u.func_int(cs, ctx->obj, i))) {
				return -r2; // user error
			}
			break;
		}

		case _FFCONF_TFLOAT: {
			double d;
			if (!ffstr_to_float(val, &d))
				return _FFCONF_ERR(cs, "float expected");

			if (d < 0 && !(flags & FFCONF_FSIGN))
				return _FFCONF_ERR(cs, "unsigned float expected");

			if (cs->arg->dst < MAX_OFF) {
				if (flags & FFCONF_F32BIT)
					*u.f32 = d;
				else
					*u.f64 = d;
			} else if (0 != (r2 = u.func_float(cs, ctx->obj, d)))
				return -r2; // user error
			break;
		}

		case _FFCONF_TBOOL: {
			int i;
			if (ffstr_ieqz(val, "true")) {
				i = 1;
			} else if (ffstr_ieqz(val, "false")) {
				i = 0;
			} else if (ffstr_to_uint32(val, &i)
				&& (i == 0 || i == 1)) {
				//
			} else {
				return _FFCONF_ERR(cs, "boolean expected");
			}

			if (cs->arg->dst < MAX_OFF)
				*u.b = !!i;
			else if (0 != (r2 = u.func_int(cs, ctx->obj, i)))
				return -r2; // user error
			break;
		}

		case FFCONF_TOBJ:
			if (r == FFCONF_RVAL_NEXT)
				return _FFCONF_ERR(cs, "only 1 object value is supported");

			if ((flags & FFCONF_FNOTEMPTY) && val->len == 0)
				return _FFCONF_ERR(cs, "value must not be empty");

			ffstr_free(&cs->objval);
			if (0 != ffconf_strval_acquire(cs->parser, &cs->objval))
				return -FFCONF_ESYS;
			break;

		default:
			return _FFCONF_ERR(cs, "value type expected in scheme");
		}
		break;
	}

	return r;
}

#undef _FFCONF_ERR

/** Convert data into a C object
scheme_flags: enum FFCONF_SCHEME_F
errmsg: (optional) error message; must free with ffstr_free()
Return 0 on success
 <0 on error: enum FFCONF_E */
static inline int ffconf_parse_object(const ffconf_arg *args, void *obj, ffstr *data, ffuint scheme_flags, ffstr *errmsg)
{
	int r, r2;
	ffltconf c = {};
	ffltconf_init(&c);

	ffconf_scheme cs = {};
	cs.flags = scheme_flags;
	cs.parser = &c.ff;
	ffconf_scheme_addctx(&cs, args, obj);

	for (;;) {
		r = ffltconf_parse(&c, data);
		if (r < 0)
			goto end;
		else if (r == 0)
			break;

		r = ffconf_scheme_process(&cs, r);
		if (r < 0)
			goto end;
	}

end:
	ffconf_scheme_destroy(&cs);
	r2 = ffltconf_fin(&c);
	if (r == 0)
		r = r2;

	if (r != 0 && errmsg != NULL) {
		ffsize cap = 0;
		const char *err = ffltconf_error(&c);
		if (r == -FFCONF_ESCHEME)
			err = cs.errmsg;
		ffstr_growfmt(errmsg, &cap, "%u:%u: %s"
			, (int)c.ff.line, (int)c.ff.linechar
			, err);
	}

	return r;
}

#ifdef _FFOS_FILE_H
static inline int ffconf_parse_file(const ffconf_arg *args, void *obj, const char *fn, ffuint scheme_flags, ffstr *errmsg, ffuint64 file_max_size)
{
	ffvec data = {};
	if (0 != fffile_readwhole(fn, &data, file_max_size)) {
		if (errmsg != NULL) {
			ffsize cap = 0;
			errmsg->len = 0;
			ffstr_growfmt(errmsg, &cap, "%s: %s", fn, fferr_strptr(fferr_last()));
		}
		return -FFCONF_ESYS;
	}
	ffstr d;
	ffstr_setstr(&d, &data);
	int r = ffconf_parse_object(args, obj, &d, scheme_flags, errmsg);
	ffvec_free(&data);
	return r;
}
#endif
