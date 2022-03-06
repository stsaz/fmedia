/** ff: conf copy
2018,2020, Simon Zolin
*/

#pragma once
#include "conf2-writer.h"

/** Copy data within context.  Useful for deferred processing. */
typedef struct ffconf_ctxcopy {
	ffconfw wr;
	uint level;
} ffconf_ctxcopy;

static inline void ffconf_ctxcopy_init(ffconf_ctxcopy *cc)
{
	ffmem_zero_obj(cc);
	ffconfw_init(&cc->wr, 0);
	cc->level = 1;
}

static inline void ffconf_ctxcopy_destroy(ffconf_ctxcopy *cc)
{
	ffconfw_close(&cc->wr);
}

/** Acquire data and reset writer's buffer.
Free with ffstr_free(). */
static inline ffstr ffconf_ctxcopy_acquire(ffconf_ctxcopy *cc)
{
	ffstr d;
	ffconfw_output(&cc->wr, &d);
	ffvec_null(&cc->wr.buf);
	return d;
}

/** Copy data.
rcode: parser's return code: enum FFCONF_R
Return 0: data chunk was copied;  >0: finished;  <0: error. */
static inline int ffconf_ctx_copy(ffconf_ctxcopy *cc, ffstr val, int rcode)
{
	int r;
	switch (rcode) {
	case FFCONF_RKEY:
		r = ffconfw_addkey(&cc->wr, &val);
		break;

	case FFCONF_RVAL:
	case FFCONF_RVAL_NEXT:
		r = ffconfw_addstr(&cc->wr, &val);
		break;

	case FFCONF_ROBJ_OPEN:
		cc->level++;
		r = ffconfw_addobj(&cc->wr, 1);
		break;

	case FFCONF_ROBJ_CLOSE:
		if (--cc->level == 0) {
			if (0 != ffconfw_fin(&cc->wr))
				return -1;
			return 1;
		}
		r = ffconfw_addobj(&cc->wr, 0);
		break;

	default:
		return -1;
	}

	if (r < 0)
		return -1;
	return 0;
}
