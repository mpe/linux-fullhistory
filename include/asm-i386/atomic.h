#ifndef __ARCH_I386_ATOMIC__
#define __ARCH_I386_ATOMIC__

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#ifdef __SMP__
#define LOCK "lock ; "
#else
#define LOCK ""
#endif

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(struct { int a[100]; } *)x)

typedef int atomic_t;

static __inline__ void atomic_add(atomic_t i, atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "addl %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "subl %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "incl %0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "decl %0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		LOCK "decl %0; sete %1"
		:"=m" (__atomic_fool_gcc(v)), "=qm" (c)
		:"m" (__atomic_fool_gcc(v)));
	return c != 0;
}

#endif
