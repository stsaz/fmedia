/** ff: configuration parser
2020, Simon Zolin
*/

/*
ffconf_init
ffconf_errstr
ffconf_fin
ffconf_parse
ffconf_strval_acquire
*/

/*
Data format:

	# one-line comment
	KEY [VALUE...]
	KEY [VALUE...] {
		...
	}

A key or value MAY be enclosed in quotes
 but MUST be enclosed in quotes if it contains whitespace ("value with space")
 or contains braces {} ("{value with braces}")
 or contains '#'
 or is empty ("").
Whitespace around a key or value is trimmed,
 but whitespace within quotes is preserved.

	key1 value-1
	key2 ""
	key3 "value 3"

A key MAY have multiple values divided by whitespace:

	key value-1 value-2 value-3

A key or value MAY contain escape-sequences with "\xXX" notation,
 or a standard escape character (\0 \b \f \r \n \t \" \\).
Characters MUST be escaped: [0..0x19] 0x7f "" \

	key value\x20value

A key or a key-value pair MAY open a new context - enclose in {} braces with the following conditions:
 * '{' MUST be on the same line
 * '}' MUST be on a new line

	key {
		...
	}
	key value {
		...
	}

Why it's better than JSON for storing configuration data:

	* doesn't require double quotes (`value` vs. `"value"`)
	* doesn't require colons (`key val` vs. `"key":"val"`)
	* doesn't require commas (`val1 val2` vs. `"val1","val2"`)
	* supports bash-like #-comments (`# this is a comment`)

Advantage over YAML:

	* doesn't require whitespace alignment, but uses {} for nested objects, like JSON

*/

#pragma once
#include <ffbase/string.h>
#include <ffbase/vector.h>

typedef struct ffconf {
	ffuint state, nextstate;
	ffvec buf; // char[]: buffer for partial input data
	ffuint ctxs; // contexts number
	char esc_char;
	ffuint clear :1;
	ffuint nextval :1;

// public:
	ffuint line, linechar; // line & character number
	ffstr val; // key or value
} ffconf;

enum FFCONF_E {
	FFCONF_ESYS = 1,
	FFCONF_EESC,
	FFCONF_ESTR,
	FFCONF_ECTX,
	FFCONF_EBADVAL,
	FFCONF_EINCOMPLETE,
	FFCONF_ESCHEME,
};

/** Get error string from code (<0) */
static inline const char* ffconf_errstr(int err)
{
	if (err >= 0)
		return "";

	static const char* const conf_err[] = {
		"FFCONF_ESYS",
		"FFCONF_EESC",
		"FFCONF_ESTR",
		"FFCONF_ECTX",
		"FFCONF_EBADVAL",
		"FFCONF_EINCOMPLETE",
		"FFCONF_ESCHEME",
	};
	return conf_err[-err - 1];
}

enum FFCONF_R {
	FFCONF_RMORE,
	FFCONF_RKEY, // KEY val
	FFCONF_RVAL, // key VAL
	FFCONF_RVAL_NEXT, // key val1 VAL2...
	FFCONF_ROBJ_OPEN, // {
	FFCONF_ROBJ_CLOSE, // }
};

static inline void ffconf_init(ffconf *c)
{
	ffmem_zero_obj(c);
	c->line = c->linechar = 1;
}

/** Get or copy string value into a user's container */
static inline int ffconf_strval_acquire(ffconf *c, ffstr *dst)
{
	if (c->val.len == 0) {
		ffstr_null(dst);
		return 0;
	}
	if (c->buf.cap != 0 && c->val.ptr == c->buf.ptr) {
		*dst = c->val;
		ffvec_free(&c->buf);
		return 0;
	}
	if (NULL == ffstr_dup2(dst, &c->val))
		return -1;
	return 0;
}

/** Add (reference or copy) character
Don't copy data if internal buffer is empty
Grow buffer by twice the existing size */
static inline int _ffconf_val_add(ffconf *c, const char *d)
{
	if (c->buf.cap == 0) {
		if (c->buf.len == 0)
			c->buf.ptr = (char*)d;
		c->buf.len++;
		return 0;
	}

	if (ffvec_isfull(&c->buf)
		&& NULL == ffvec_growtwiceT(&c->buf, 128, char))
		return -1;

	*ffstr_push(&c->buf) = *d;
	return 0;
}

/** Add (copy) a byte */
static inline int _ffconf_val_add_b(ffconf *c, ffuint b)
{
	if (NULL == ffvec_growT(&c->buf, 1, char))
		return -1;
	*ffstr_push(&c->buf) = b;
	return 0;
}

/**
Return enum FFCONF_R;
  <0 on error: enum FFCONF_E */
static inline int ffconf_parse(ffconf *c, ffstr *data)
{
	enum {
		I_SPC_BEFO_KEY, // -> I_KEY
		I_SPC_BEFO_VAL, // -> I_VAL
		I_CMT, // -> I_SPC_BEFO_KEY
		I_KEY, // -> I_KEY_QUOT, I_ESC, I_SPC_BEFO_KEY, I_SPC_BEFO_VAL
		I_VAL, // -> I_VAL_QUOT, I_ESC, I_SPC_BEFO_KEY, I_SPC_AFTE_VAL
		I_KEY_QUOT, // -> I_ESC, I_SPC_AFTE_KEY
		I_VAL_QUOT, // -> I_ESC, I_SPC_AFTE_VAL
		I_ESC, I_ESC_X1, I_ESC_X2, // -> I_KEY, I_VAL, I_KEY_QUOT, I_VAL_QUOT
		I_SPC_AFTE_KEY, // -> I_SPC_BEFO_KEY
		I_SPC_AFTE_VAL, // -> I_SPC_BEFO_VAL
	};

	if (data == NULL) {
		if (!(c->ctxs == 0
			&& (c->state == I_SPC_BEFO_KEY
				|| c->state == I_SPC_AFTE_KEY
				|| c->state == I_SPC_AFTE_VAL
				|| (c->state == I_SPC_BEFO_VAL && c->nextval))))
			return -FFCONF_EINCOMPLETE;
		return 0;
	}

	int r = 0;
	ffuint st = c->state;
	ffsize i;

	if (c->clear) {
		c->clear = 0;
		ffstr_null(&c->val);
		ffvec_free(&c->buf);
	}

	for (i = 0;  i != data->len;) {

		ffuint ch = (ffbyte)data->ptr[i];

		switch (st) {

		case I_SPC_BEFO_KEY:
		case I_SPC_BEFO_VAL:
			switch (ch) {

			case ' ': case '\t':
				break;

			case '\r': case '\n':
				st = I_SPC_BEFO_KEY;
				break;

			case '#':
				st = I_CMT;
				break;

			case '{':
				if (st != I_SPC_BEFO_VAL)
					return -FFCONF_ESTR; // line starts with '{'

				c->ctxs++;
				r = FFCONF_ROBJ_OPEN;
				st = I_SPC_BEFO_KEY;
				break;

			case '}':
				if (st != I_SPC_BEFO_KEY)
					return -FFCONF_ESTR; // '}' must be on a new line
				if (c->ctxs == 0)
					return -FFCONF_ECTX; // no context to close

				c->ctxs--;
				r = FFCONF_ROBJ_CLOSE;
				st = I_SPC_BEFO_KEY;
				break;

			case '"':
				st = (st == I_SPC_BEFO_KEY) ? I_KEY_QUOT : I_VAL_QUOT;
				break;

			default:
				st = (st == I_SPC_BEFO_KEY) ? I_KEY : I_VAL;
				continue;
			}
			break;

		case I_CMT:
			if (ch == '\n')
				st = I_SPC_BEFO_KEY;
			break;

		case I_KEY:
		case I_VAL:
			switch (ch) {

			case ' ': case '\t':
				r = st;
				st = I_SPC_BEFO_VAL;
				break;

			case '\r': case '\n':
				r = st;
				st = I_SPC_BEFO_KEY;
				break;

			case '\\':
				c->nextstate = st;
				st = I_ESC;
				break;

			case '{': case '}':
				return -FFCONF_ESTR; // unquoted key or value contains {}

			default:
				if (ch < 0x20 || ch == 0x7f)
					return -FFCONF_ESTR;
				if (0 != _ffconf_val_add(c, &data->ptr[i]))
					return -FFCONF_ESYS;
				break;
			}

			if (r == I_KEY) {
				r = FFCONF_RKEY;
				c->nextval = 0;
				ffstr_set2(&c->val, &c->buf);
				c->clear = 1;

			} else if (r == I_VAL) {
				r = (!c->nextval) ? FFCONF_RVAL : FFCONF_RVAL_NEXT;
				c->nextval = 1;
				ffstr_set2(&c->val, &c->buf);
				c->clear = 1;
			}
			break;

		case I_KEY_QUOT:
		case I_VAL_QUOT:
			switch (ch) {
			case '"':
				if (st == I_KEY_QUOT) {
					r = FFCONF_RKEY;
					c->nextval = 0;
					ffstr_set2(&c->val, &c->buf);
					c->clear = 1;
					st = I_SPC_AFTE_KEY;
					break;
				}

				r = (!c->nextval) ? FFCONF_RVAL : FFCONF_RVAL_NEXT;
				c->nextval = 1;
				ffstr_set2(&c->val, &c->buf);
				c->clear = 1;
				st = I_SPC_AFTE_VAL;
				break;

			case '\\':
				c->nextstate = st;
				st = I_ESC;
				break;

			default:
				if (ch < 0x20 || ch == 0x7f)
					return -FFCONF_ESTR;

				if (0 != _ffconf_val_add(c, &data->ptr[i]))
					return -FFCONF_ESYS;
			}
			break;

		case I_ESC: {
			static const char conf_esc_char[] = "0\"\\bfrnt";
			static const char conf_esc_byte[] = "\0\"\\\b\f\r\n\t";
			ffssize pos;
			if (0 <= (pos = ffs_findchar(conf_esc_char, FFS_LEN(conf_esc_char), ch))) {
				if (0 != _ffconf_val_add_b(c, conf_esc_byte[pos]))
					return -FFCONF_ESYS;
				st = c->nextstate;

			} else if (ch == 'x') {
				st = I_ESC_X1;

			} else {
				return -FFCONF_EESC; // unknown escape character after backslash
			}
			break;
		}

		case I_ESC_X1:
			c->esc_char = ch;
			st = I_ESC_X2;
			break;

		case I_ESC_X2: {
			ffuint hi = ffchar_tohex(c->esc_char);
			ffuint lo = ffchar_tohex(ch);
			if ((int)hi < 0 || (int)lo < 0)
				return -FFCONF_EESC; // the 2 following digits after 'x' are not hex number
			if (0 != _ffconf_val_add_b(c, (hi<<4) | lo))
				return -FFCONF_ESYS;
			st = c->nextstate;
			break;
		}

		case I_SPC_AFTE_KEY:
		case I_SPC_AFTE_VAL:
			switch (ch) {
			case ' ': case '\t':
				st = I_SPC_BEFO_VAL;
				break;

			case '\r': case '\n':
				st = I_SPC_BEFO_KEY;
				break;

			default:
				return -FFCONF_ESTR; // non-whitespace follows a closing quote
			}
			break;
		}

		i++;
		c->linechar++;
		if (ch == '\n') {
			c->line++;
			c->linechar = 1;
		}

		if (r != 0)
			goto end;
	}

	// copy referenced data to internal buffer
	if (c->buf.len != 0 && c->buf.cap == 0)
		if (NULL == ffvec_growT(&c->buf, 0, char))
			return -FFCONF_ESYS;

end:
	c->state = st;
	ffstr_shift(data, i);
	return r;
}

static inline int ffconf_parse3(ffconf *c, ffstr *input, ffstr *output)
{
	int r = ffconf_parse(c, input);
	*output = c->val;
	return r;
}

/**
Return 0 on success;
  <0 on error: enum FFCONF_E */
static inline int ffconf_fin(ffconf *c)
{
	int r = ffconf_parse(c, NULL);
	ffvec_free(&c->buf);
	return r;
}
