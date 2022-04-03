#pragma once
#include "string.h"
#include "ffos-compat/atomic.h"

/** Circular array of pointers with fixed number of elements.
Readers and writers can run in parallel.
Empty buffer: [(r,w). . . .]
Full buffer: [(r)E1 E2 E3 (w).]
Full buffer: [(w). (r)E1 E2 E3]
Overlapped:  [. (r)E1 (w). .]
Overlapped:  [E2 (w). . (r)E1]
*/
struct ffring {
	void **d;
	size_t cap;
	ffatomic whead, wtail;
	ffatomic r;
};
typedef struct ffring ffring;

/**
@size: power of 2. */
static inline int ffring_create(ffring *r, size_t size, uint align);

static inline void ffring_destroy(ffring *r)
{
	ffmem_alignfree(r->d);
}

static inline int _ffring_write(ffring *r, void *p, uint single);

/** Add element to ring buffer.
Return 0 on success;  <0 if full. */
static inline int ffring_write(ffring *r, void *p)
{
	return _ffring_write(r, p, 0);
}

/** Read element from ring buffer.
Return 0 on success;  <0 if empty. */
static inline int ffring_read(ffring *r, void **p);

static inline size_t ffint_increset2(size_t n, size_t cap)
{
	return (n + 1) & (cap - 1);
}

static inline int ffring_empty(ffring *r)
{
	return (ffatom_get(&r->r) == ffatom_get(&r->wtail));
}


typedef struct ffringbuf {
	char *data;
	size_t cap;
	size_t r, w;
	fflock lk;
} ffringbuf;

/**
@cap: power of 2. */
static inline void ffringbuf_init(ffringbuf *r, void *p, size_t cap)
{
	FF_ASSERT(0 == (cap & (cap - 1)));
	r->data = (char*)p;
	r->cap = cap;
	r->r = r->w = 0;
	fflk_init(&r->lk);
}

#define ffringbuf_data(r)  ((r)->data)

/** Return # of sequential bytes available to read. */
static inline size_t ffringbuf_canread_seq(ffringbuf *r)
{
	return (r->w >= r->r) ? r->w - r->r : r->cap - r->r;
}

/** Return # of bytes available to write. */
static inline size_t ffringbuf_canwrite(ffringbuf *r)
{
	return (r->r - r->w - 1) & (r->cap - 1);
}

/** Return # of sequential bytes available to write. */
static inline size_t ffringbuf_canwrite_seq(ffringbuf *r)
{
	if (r->r > r->w)
		return r->r - r->w - 1;
	return (r->r == 0) ? r->cap - r->w - 1 : r->cap - r->w;
}

/** Append (overwrite) data. */
static inline void ffringbuf_overwrite(ffringbuf *r, const void *data, size_t len);

/** Get chunk of sequential data. */
static inline void ffringbuf_readptr(ffringbuf *r, ffstr *dst, size_t len);


static inline int ffring_create(ffring *r, size_t size, uint align)
{
	FF_ASSERT(0 == (size & (size - 1)));
	if (NULL == (r->d = ffmem_align(size * sizeof(void*), align)))
		return -1;
	r->cap = size;
	return 0;
}

/*
. Try to reserve the space for the data to add.
  If another writer has reserved this space before us, we try again.
. Write new data
. Wait for the previous writers to finish their job
. Finalize: update writer-tail pointer
*/
static inline int _ffring_write(ffring *r, void *p, uint single)
{
	size_t head_old, head_new;

	for (;;) {
		head_old = ffatom_get(&r->whead);
		head_new = ffint_increset2(head_old, r->cap);
		if (head_new == ffatom_get(&r->r))
			return -1;
		if (single)
			break;
		if (ffatom_cmpset(&r->whead, head_old, head_new))
			break;
		// other writer has added another element
	}

	r->d[head_old] = p;

	if (!single) {
		while (ffatom_get(&r->wtail) != head_old) {
			ffcpu_pause();
		}
	}

	ffcpu_fence_release(); // the element is complete when reader sees it
	ffatom_set(&r->wtail, head_new);
	return 0;
}

static inline int ffring_read(ffring *r, void **p)
{
	void *rc;
	size_t rr, rnew;

	for (;;) {
		rr = ffatom_get(&r->r);
		if (rr == ffatom_get(&r->wtail))
			return -1;
		ffcpu_fence_acquire(); // if we see an unread element, it's complete
		rc = r->d[rr];
		rnew = ffint_increset2(rr, r->cap);
		if (ffatom_cmpset(&r->r, rr, rnew))
			break;
		// other reader has read this element
	}

	*p = rc;
	return 0;
}


/** Add and reset to 0 on reaching the limit. */
#define ffint_add_reset2(num, add, cap) \
	(((num) + (add)) & ((cap) - 1))

static inline void ffringbuf_overwrite(ffringbuf *r, const void *data, size_t len)
{
	if (len >= r->cap) {
		data = (byte*)data + len - (r->cap - 1);
		len = r->cap - 1;
	}
	size_t n = ffmin(r->cap - r->w, len);
	ffmemcpy(r->data + r->w, data, n);
	if (len != n)
		ffmemcpy(r->data, (byte*)data + n, len - n);
	size_t ow = ffmax((ssize_t)(len - ffringbuf_canwrite(r)), 0);
	r->w = ffint_add_reset2(r->w, len, r->cap);
	r->r = ffint_add_reset2(r->r, ow, r->cap);
}

static inline void ffringbuf_readptr(ffringbuf *r, ffstr *dst, size_t len)
{
	size_t n = ffmin(len, ffringbuf_canread_seq(r));
	ffstr_set(dst, r->data + r->r, n);
	r->r = ffint_add_reset2(r->r, n, r->cap);
}
