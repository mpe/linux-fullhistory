/* atomic.h: These still suck, but the I-cache hit rate is higher.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ARCH_SPARC_ATOMIC__
#define __ARCH_SPARC_ATOMIC__

typedef int atomic_t;

#ifdef __KERNEL__
#include <asm/system.h>
#include <asm/psr.h>

/* We do the bulk of the actual work out of line in two common
 * routines in assembler, see arch/sparc/lib/atomic.S for the
 * "fun" details.
 */

/* Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) ((struct { int a[100]; } *)x)

static __inline__ void atomic_add(atomic_t i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register atomic_t increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory");
}

static __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register atomic_t increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory");
}

static __inline__ int atomic_add_return(atomic_t i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register atomic_t increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_add
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory");

	return increment;
}

static __inline__ int atomic_sub_return(atomic_t i, atomic_t *v)
{
	register atomic_t *ptr asm("g1");
	register atomic_t increment asm("g2");

	ptr = (atomic_t *) __atomic_fool_gcc(v);
	increment = i;

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___atomic_sub
	 add	%%o7, 8, %%o7
"	: "=&r" (increment)
	: "0" (increment), "r" (ptr)
	: "g3", "g4", "g7", "memory");

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
