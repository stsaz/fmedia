/** Operations with numbers and pointers.
Copyright (c) 2016 Simon Zolin
*/

#define FFCNT  FF_COUNT
#define FFOFF  FF_OFF
#define FF_GETPTR  FF_STRUCTPTR
#define FF_WRITEONCE  FFINT_WRITEONCE
#define FF_READONCE  FFINT_READONCE


/** Set new value and return old value. */
#define FF_SWAP(obj, newval) \
({ \
	__typeof__(*(obj)) _old = *(obj); \
	*(obj) = newval; \
	_old; \
})

#define FF_BIT32(bit)  (1U << (bit))

#define ffabs  ffint_abs

/** Set the minimum value.
The same as: dst = min(dst, src) */
#define ffint_setmin(dst, src) \
do { \
	if ((dst) > (src)) \
		(dst) = (src); \
} while (0)


#define ffhton32(i)  ffint_be_cpu32(i)
#define ffhton16(i)  ffint_be_cpu16(i)
#define ff_align_power2  ffint_align_power2
#define ff_align_floor2  ffint_align_floor2
#define ff_align_floor  ffint_align_floor
#define ff_align_ceil  ffint_align_ceil

#if defined FF_SAFECAST_SIZE_T && defined FF_64
	/** Safe cast 'size_t' to 'uint'. */
	#define FF_TOINT(i)  (uint)ffmin(i, (uint)-1)
#else
	#define FF_TOINT(i)  (uint)(i)
#endif
