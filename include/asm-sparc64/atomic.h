/* $Id: atomic.h,v 1.6 1996/12/12 15:40:22 davem Exp $
 * atomic.h: Thankfully the V9 is at least reasonable for this
 *           stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ARCH_SPARC64_ATOMIC__
#define __ARCH_SPARC64_ATOMIC__

/* Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) ((struct { int a[100]; } *)x)

typedef int atomic_t;

extern __inline__ void atomic_add(atomic_t i, atomic_t *v)
{
	unsigned long temp0, temp1;
	__asm__ __volatile__("
	lduw		[%2], %0
1:
	add		%0, %3, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
" 	: "=&r" (temp0), "=&r" (temp1), "=r" (__atomic_fool_gcc(v))
	: "ir" (i), "r" (__atomic_fool_gcc(v)));
}

extern __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	unsigned long temp0, temp1;
	__asm__ __volatile__("
	lduw		[%2], %0
1:
	sub		%0, %3, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
"	: "=&r" (temp0), "=&r" (temp1), "=r" (__atomic_fool_gcc(v))
	: "ir" (i), "r" (__atomic_fool_gcc(v)));
}

/* Same as above, but return the result value. */
extern __inline__ long atomic_add_return(atomic_t i, atomic_t *v)
{
	long temp0, result;
	__asm__ __volatile__("
	lduw		[%2], %0
1:
	add		%0, %3, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
"	: "=&r" (temp0), "=&r" (result), "=r" (__atomic_fool_gcc(v))
	: "ir" (i), "r" (__atomic_fool_gcc(v)));
	return result;
}

extern __inline__ long atomic_sub_return(atomic_t i, atomic_t *v)
{
	long temp0, result;
	__asm__ __volatile__("
	lduw		[%2], %0
1:
	sub		%0, %3, %1
	cas		[%2], %0, %1
	cmp		%0, %1
	bne,a,pn	%%icc, 1b
	 lduw		[%2], %0
2:
"	: "=&r" (temp0), "=&r" (result), "=r" (__atomic_fool_gcc(v))
	: "ir" (i), "r" (__atomic_fool_gcc(v)));
	return result;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

#endif /* !(__ARCH_SPARC64_ATOMIC__) */
