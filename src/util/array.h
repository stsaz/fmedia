#pragma once
#include "string.h"
#include "chain.h"
#include <ffbase/vector.h>

/** FOREACH() for static array, e.g. int ar[4] */
#define FFARRS_FOREACH(ar, it) \
	for (it = (ar);  it != (ar) + FFCNT(ar);  it++)

#define FFARR_WALKT  FFSLICE_WALK_T

typedef ffvec ffarr;

/** Set a buffer. */
#define ffarr_set(ar, data, n) \
do { \
	(ar)->ptr = data; \
	(ar)->len = n; \
} while(0)

#define ffarr_set3(ar, d, _len, _cap) \
do { \
	(ar)->ptr = d; \
	(ar)->len = _len; \
	(ar)->cap = _cap; \
} while(0)

/** Set null array. */
#define ffarr_null(ar) \
do { \
	(ar)->ptr = NULL; \
	(ar)->len = (ar)->cap = 0; \
} while (0)

/** Acquire array data. */
#define ffarr_acq(dst, src) \
do { \
	*(dst) = *(src); \
	(src)->cap = 0; \
} while (0)

#define _ffarr_item(ar, idx, elsz)  ((ar)->ptr + idx * elsz)
#define ffarr_itemT(ar, idx, T)  (&((T*)(ar)->ptr)[idx])

/** The last element in array. */
#define ffarr_lastT(ar, T)  (&((T*)(ar)->ptr)[(ar)->len - 1])

/** The tail of array. */
#define _ffarr_end(ar, elsz)  _ffarr_item(ar, ar->len, elsz)
#define ffarr_end(ar)  ((ar)->ptr + (ar)->len)

/** Get the edge of allocated buffer. */
#define ffarr_edge(ar)  ((ar)->ptr + (ar)->cap)

/** The number of free elements. */
#define ffarr_unused(ar)  ((ar)->cap - (ar)->len)

#define _ffarr_realloc(ar, newlen, elsz) \
	ffvec_realloc((ffvec*)(ar), newlen, elsz)

/** Reallocate array memory if new size is larger.
Pointing buffer: transform into an allocated buffer, copying data.
Return NULL on error. */
#define ffarr_realloc(ar, newlen) \
	_ffarr_realloc((ffarr*)(ar), newlen, sizeof(*(ar)->ptr))

static inline void * _ffarr_alloc(ffarr *ar, size_t len, size_t elsz) {
	ffarr_null(ar);
	return _ffarr_realloc(ar, len, elsz);
}

/** Allocate memory for an array. */
#define ffarr_alloc(ar, len) \
	_ffarr_alloc((ffarr*)(ar), (len), sizeof(*(ar)->ptr))

#define ffarr_allocT(ar, len, T) \
	_ffarr_alloc(ar, len, sizeof(T))

#define ffarr_reallocT(ar, len, T) \
	_ffarr_realloc(ar, len, sizeof(T))

/** Deallocate array memory. */
#define ffarr_free(ar)  ffvec_free(ar)

#define FFARR_FREE_ALL(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(__it); \
	} \
	ffarr_free(a); \
} while (0)

#define FFARR_FREE_ALL_PTR(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(*__it); \
	} \
	ffarr_free(a); \
} while (0)

#define _ffarr_push(ar, elsz)  ffvec_push((ffvec*)(ar), elsz)

#define ffarr_push(ar, T) \
	(T*)_ffarr_push((ffarr*)ar, sizeof(T))

#define ffarr_pushT(ar, T) \
	(T*)_ffarr_push(ar, sizeof(T))

#define ffarr_pushgrowT(ar, lowat, T) \
	ffvec_pushT(ar, T)

/** Add items into array.  Reallocate memory, if needed.
Return the tail.
Return NULL on error. */
static inline void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz)
{
	if (num != ffvec_add((ffvec*)ar, src, num, elsz))
		return NULL;
	return (void*)((char*)ar->ptr + ar->len * elsz);
}

#define ffarr_append(ar, src, num) \
	_ffarr_append((ffarr*)ar, src, num, sizeof(*(ar)->ptr))

/** Allocate and copy data from memory pointed by 'a.ptr'. */
#define ffarr_copyself(a) \
do { \
	if ((a)->cap == 0 && (a)->len != 0) \
		ffarr_realloc(a, (a)->len); \
} while (0)

typedef ffarr ffstr3;

static inline void ffstr_acqstr3(ffstr *dst, ffstr3 *src) {
	dst->ptr = (char*)src->ptr;
	dst->len = src->len;
	ffarr_null(src);
}

#define ffstr_alcopystr(dst, src)  ffstr_dup(dst, (src)->ptr, (src)->len)

#define ffstr_catfmt(s, fmt, ...)  ffvec_addfmt((ffvec*)s, fmt, ##__VA_ARGS__)

static inline size_t ffstr_fmt(ffstr3 *s, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	s->len = 0;
	ffsize r = ffstr_growfmtv((ffstr*)s, &s->cap, fmt, args);
	va_end(args);
	return r;
}

#define ffsz_alfmtv  ffsz_allocfmtv
#define ffsz_alfmt  ffsz_allocfmt


/** Memory block that can be linked with another block.
BLK0 <-> BLK1 <-> ... */
typedef struct ffmblk {
	ffarr buf;
	ffchain_item sib;
} ffmblk;

/** Allocate and add new block into the chain. */
static inline ffmblk* ffmblk_chain_push(ffchain *blocks)
{
	ffmblk *mblk;
	if (NULL == (mblk = ffmem_allocT(1, ffmblk)))
		return NULL;
	ffarr_null(&mblk->buf);
	ffchain_add(blocks, &mblk->sib);
	return mblk;
}

/** Get the last block in chain. */
static inline ffmblk* ffmblk_chain_last(ffchain *blocks)
{
	if (ffchain_empty(blocks))
		return NULL;
	ffchain_item *blk = ffchain_last(blocks);
	return FF_GETPTR(ffmblk, sib, blk);
}

static inline void ffmblk_free(ffmblk *m)
{
	ffarr_free(&m->buf);
	ffmem_free(m);
}
