/** ltconf--ffconf bridge
2022, Simon Zolin
*/

/*
ffltconf_init
ffltconf_fin
ffltconf_parse ffltconf_parse3
ffltconf_error
*/

#pragma once
#include <util/ltconf.h>
#include <util/conf2.h>

typedef struct ffltconf {
	struct ltconf lt;
	ffconf ff;
} ffltconf;

static inline void ffltconf_init(ffltconf *c)
{
	ffmem_zero_obj(c);
}

static inline int ffltconf_parse(ffltconf *c, ffstr *input)
{
	for (;;) {
		int r = ltconf_read(&c->lt, input, &c->ff.val);
		c->ff.line = ltconf_line(&c->lt);
		c->ff.linechar = ltconf_col(&c->lt);

		if (r == LTCONF_CHUNK || c->ff.buf.len != 0) {
			// store new data chunk in buffer
			ffvec_add2(&c->ff.buf, &c->ff.val, 1);
			ffstr_setstr(&c->ff.val, &c->ff.buf);
		}

		switch (r) {
		case LTCONF_CHUNK:
			continue;

		case LTCONF_MORE:
			r = FFCONF_RMORE;
			break;

		case LTCONF_KEY:
			r = FFCONF_RKEY;
			if (ffstr_eqcz(&c->ff.val, "}")
				&& !(c->lt.flags & LTCONF_FQUOTED)) {
				if (c->ff.ctxs == 0)
					return -FFCONF_ECTX;
				c->ff.ctxs--;
				r = FFCONF_ROBJ_CLOSE;
			}
			c->ff.buf.len = 0;
			break;

		case LTCONF_VAL:
			r = FFCONF_RVAL;
			if (ffstr_eqcz(&c->ff.val, "{")
				&& !(c->lt.flags & LTCONF_FQUOTED)) {
				c->ff.ctxs++;
				r = FFCONF_ROBJ_OPEN;
			}
			c->ff.buf.len = 0;
			break;

		case LTCONF_VAL_NEXT:
			r = FFCONF_RVAL_NEXT;
			if (ffstr_eqcz(&c->ff.val, "{")
				&& !(c->lt.flags & LTCONF_FQUOTED)) {
				c->ff.ctxs++;
				r = FFCONF_ROBJ_OPEN;
			}
			c->ff.buf.len = 0;
			break;

		case LTCONF_ERROR:
			r = -FFCONF_ESTR;
			break;

		default:
			FF_ASSERT(0);
		}

		return r;
	}
}

static inline int ffltconf_parse3(ffltconf *c, ffstr *input, ffstr *output)
{
	int r = ffltconf_parse(c, input);
	*output = c->ff.val;
	return r;
}

static inline int ffltconf_fin(ffltconf *c)
{
	ffvec_free(&c->ff.buf);
	if (c->ff.ctxs != 0)
		return -FFCONF_ECTX;
	return 0;
}

static inline const char* ffltconf_error(ffltconf *c)
{
	return ltconf_error(&c->lt);
}
