/* $Id: atomic.h,v 1.18 1997/08/07 03:38:31 davem Exp $
 * atomic.h: Thankfully the V9 is at least reasonable for this
 *           stuff.
 *
 * Copyright (C) 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ARCH_SPARC64_ATOMIC__
#define __ARCH_SPARC64_ATOMIC__

/* Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) ((struct { int a[100]; } *)x)

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v, i)	(((v)->counter) = i)

extern __inline__ void atomic_add(int i, atomic_t *v)
{
	__asm__ __volatile__("
1:	lduw		[%1], %%g5
	add		%%g5, %0, %%g7
	cas		[%1], %%g5, %%g7
	sub		%%g5, %%g7, %%g5
	brnz,pn		%%g5, 1b
	 nop"
	: /* No outputs */
	: "HIr" (i), "r" (__atomic_fool_gcc(v))
	: "g5", "g7", "memory");
}

extern __inline__ void atomic_sub(int i, atomic_t *v)
{
	__asm__ __volatile__("
1:	lduw		[%1], %%g5
	sub		%%g5, %0, %%g7
	cas		[%1], %%g5, %%g7
	sub		%%g5, %%g7, %%g5
	brnz,pn		%%g5, 1b
	 nop"
	: /* No outputs */
	: "HIr" (i), "r" (__atomic_fool_gcc(v))
	: "g5", "g7", "memory");
}

/* Same as above, but return the result value. */
extern __inline__ int atomic_add_return(int i, atomic_t *v)
{
	unsigned long oldval;
	__asm__ __volatile__("
1:	lduw		[%2], %%g5
	add		%%g5, %1, %%g7
	cas		[%2], %%g5, %%g7
	sub		%%g5, %%g7, %%g5
	brnz,pn		%%g5, 1b
	 add		%%g7, %1, %0"
	: "=&r" (oldval)
	: "HIr" (i), "r" (__atomic_fool_gcc(v))
	: "g5", "g7", "memory");
	return (int)oldval;
}

extern __inline__ int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long oldval;
	__asm__ __volatile__("
1:	lduw		[%2], %%g5
	sub		%%g5, %1, %%g7
	cas		[%2], %%g5, %%g7
	sub		%%g5, %%g7, %%g5
	brnz,pn		%%g5, 1b
	 sub		%%g7, %1, %0"
	: "=&r" (oldval)
	: "HIr" (i), "r" (__atomic_fool_gcc(v))
	: "g5", "g7", "memory");
	return (int)oldval;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

#endif /* !(__ARCH_SPARC64_ATOMIC__) */
