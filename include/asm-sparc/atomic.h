/* atomic.h: These still suck, but the I-cache hit rate is higher.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ARCH_SPARC_ATOMIC__
#define __ARCH_SPARC_ATOMIC__

#ifdef __SMP__
/* This is a temporary measure. -DaveM */
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	{ (i << 8) }

#ifdef __KERNEL__
#include <asm/system.h>
#include <asm/psr.h>

/* We do the bulk of the actual work out of line in two common
 * routines in assembler, see arch/sparc/lib/atomic.S for the
 * "fun" details.
 *
 * For SMP the trick is you embed the spin lock byte within
 * the word, use the low byte so signedness is easily retained
 * via a quick arithmetic shift.  It looks like this:
 *
 *	----------------------------------------
 *	| signed 24-bit counter value |  lock  |  atomic_t
 *	----------------------------------------
 *	 31                          8 7      0
 */

static __inline__ int atomic_read(atomic_t *v)
{
	int val;

	__asm__ __volatile__("sra	%1, 0x8, %0"
			     : "=r" (val)
			     : "r" (v->counter));
	return val;
}
#define atomic_set(v, i)	(((v)->counter) = ((i) << 8))

/* Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) ((struct { int a[100]; } *)x)

static __inline__ void atomic_add(int i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");
	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory", "cc");
}

static __inline__ void atomic_sub(int i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory", "cc");
}

static __inline__ int atomic_add_return(int i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

static __inline__ int atomic_sub_return(int i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register int increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory", "cc");

	return increment;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i, v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

#endif /* !(__KERNEL__) */

#endif /* !(__ARCH_SPARC_ATOMIC__) */
