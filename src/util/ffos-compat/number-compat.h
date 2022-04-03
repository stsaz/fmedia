/** Operations with numbers and pointers.
Copyright (c) 2016 Simon Zolin
*/

#define FFCNT  FF_COUNT
#define FFOFF  FF_OFF
#define FF_GETPTR  FF_STRUCTPTR
#define FF_WRITEONCE  FFINT_WRITEONCE
#define FF_READONCE  FFINT_READONCE

#define FF_SIZEOF(struct_name, member)  sizeof(((struct_name*)0)->member)


/** Set new value and return old value. */
#define FF_SWAP(obj, newval) \
({ \
	__typeof__(*(obj)) _old = *(obj); \
	*(obj) = newval; \
	_old; \
})

/** Swap 2 objects. */
#define FF_SWAP2(ptr1, ptr2) \
do { \
	__typeof__(*(ptr1)) _tmp = *(ptr1); \
	*(ptr1) = *(ptr2); \
	*(ptr2) = _tmp; \
} while (0)


#define FF_CMPSET(obj, old, newval) \
	((*(obj) == (old)) ? *(obj) = (newval), 1 : 0)


#define FF_BIT32(bit)  (1U << (bit))
#define FF_BIT64(bit)  (1ULL << (bit))

#define FF_LO32(i64)  ((uint)((i64) & 0xffffffff))
#define FF_HI32(i64)  ((uint)(((i64) >> 32) & 0xffffffff))


/** Get the maximum signed integer number. */
static inline int64 ffmaxi(int64 a, int64 b)
{
	return ffmax(a, b);
}

#define ffabs  ffint_abs

/** Set the minimum value.
The same as: dst = min(dst, src) */
#define ffint_setmin(dst, src) \
do { \
	if ((dst) > (src)) \
		(dst) = (src); \
} while (0)

/** Set the maximum value.
The same as: dst = max(dst, src) */
#define ffint_setmax(dst, src) \
do { \
	if ((dst) < (src)) \
		(dst) = (src); \
} while (0)


#define ffhton32(i)  ffint_be_cpu32(i)
#define ffhton16(i)  ffint_be_cpu16(i)
#define ff_align_power2  ffint_align_power2
#define ff_align_floor2  ffint_align_floor2
#define ff_align_ceil2  ffint_align_ceil2
#define ff_align_floor  ffint_align_floor
#define ff_align_ceil  ffint_align_ceil

#if defined FF_SAFECAST_SIZE_T && defined FF_64
	/** Safe cast 'size_t' to 'uint'. */
	#define FF_TOINT(i)  (uint)ffmin(i, (uint)-1)
#else
	#define FF_TOINT(i)  (uint)(i)
#endif
