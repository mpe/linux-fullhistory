/* atomic.h: These really suck for now.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ARCH_SPARC_ATOMIC__
#define __ARCH_SPARC_ATOMIC__

#ifdef __SMP__
#include <asm/smp.h>
#include <asm/smp_lock.h>
#endif

typedef int atomic_t;

static __inline__ void atomic_add(atomic_t i, atomic_t *v)
{
	unsigned long flags;

	save_flags(flags); cli();
	*v += i;
	restore_flags(flags);
}

static __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	unsigned long flags;

	save_flags(flags); cli();
	*v -= i;
	restore_flags(flags);
}

static __inline__ int atomic_sub_and_test(atomic_t i, atomic_t *v)
{
	unsigned long flags, result;

	save_flags(flags); cli();
	*v -= i;
	result = (*v == 0);
	restore_flags(flags);
	return result;
}

static __inline__ void atomic_inc(atomic_t *v)
{
	atomic_add(1, v);
}

static __inline__ void atomic_dec(atomic_t *v)
{
	atomic_sub(1, v);
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	return atomic_sub_and_test(1, v);
}

#endif /* !(__ARCH_SPARC_ATOMIC__) */
