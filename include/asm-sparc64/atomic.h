/* $Id: atomic.h,v 1.19 1999/07/03 22:11:17 davem Exp $
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

#define atomic_add_return(__i, __v) \
({	register atomic_t *__V asm("g1"); \
	register int __I asm("g2"); \
	__V = (__v); __I = (__i); \
	__asm__ __volatile__("sethi	%%hi(__atomic_add), %%g3\n\t" \
			     "jmpl	%%g3 + %%lo(__atomic_add), %%g3\n\t" \
			     " nop\n1:" \
			     : "=&r" (__I) \
			     : "0" (__I), "r" (__V) \
			     : "g3", "g5", "g7", "cc", "memory"); \
	__I; \
})

#define atomic_sub_return(__i, __v) \
({	register atomic_t *__V asm("g1"); \
	register int __I asm("g2"); \
	__V = (__v); __I = (__i); \
	__asm__ __volatile__("sethi	%%hi(__atomic_sub), %%g3\n\t" \
			     "jmpl	%%g3 + %%lo(__atomic_sub), %%g3\n\t" \
			     " nop\n1:" \
			     : "=&r" (__I) \
			     : "0" (__I), "r" (__V) \
			     : "g3", "g5", "g7", "cc", "memory"); \
	__I; \
})

#define atomic_add(i, v) atomic_add_return(i, v)
#define atomic_sub(i, v) atomic_sub_return(i, v)

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

#endif /* !(__ARCH_SPARC64_ATOMIC__) */
