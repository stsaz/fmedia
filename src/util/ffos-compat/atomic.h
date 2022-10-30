/** Atomic operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include "types.h"
#include <ffbase/lock.h>
#include <ffbase/atomic.h>

typedef struct { uint val; } ffatomic32;

#if defined FF_X86
static FFINL size_t ffatom32_swap(ffatomic32 *a, uint val)
{
	__asm volatile(
		"xchgl %0, %1;"
		: "+r" (val), "+m" (a->val)
		: : "memory", "cc");
	return val;
}

#elif defined FF_AMD64
/** Set new value and return old value. */
static FFINL size_t ffatom_swap(ffatomic *a, size_t val)
{
	__asm volatile(
		"xchgq %0, %1;"
		: "+r" (val), "+m" (a->val)
		: : "memory", "cc");
	return val;
}

#endif

/** Set new value. */
#define ffatom_set(a, set)  FF_WRITEONCE((a)->val, set)

/** Get value. */
#define ffatom_get(a)  FF_READONCE((a)->val)

/** Increment and return new value. */
#define ffatom_incret(a)  (ffint_fetch_add(&(a)->val, 1) + 1)

/** Decrement and return new value. */
#define ffatom_decret(a)  (ffint_fetch_add(&(a)->val, -1) - 1)

#define ffatom32_inc(a)  ffint_fetch_add(&(a)->val, 1)
#define ffatom32_dec(a)  ffint_fetch_add(&(a)->val, -1)
#define ffatom32_decret(a)  (ffint_fetch_add(&(a)->val, -1) - 1)

static FFINL int ffatom_cmpset(ffatomic *a, size_t old, size_t newval)
{
	return (old == ffint_cmpxchg(&a->val, old, newval));
}

#define ffcpu_yield  ffthread_yield
#define fflk_setup()
#define fflk_init  fflock_init
#define fflk_lock  fflock_lock
#define fflk_trylock  fflock_trylock
#define fflk_unlock  fflock_unlock
