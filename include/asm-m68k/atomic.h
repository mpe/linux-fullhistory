#ifndef __ARCH_M68K_ATOMIC__
#define __ARCH_M68K_ATOMIC__

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * We do not have SMP m68k systems, so we don't have to deal with that.
 */

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
		"addl %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_sub(atomic_t i, atomic_t *v)
{
	__asm__ __volatile__(
		"subl  %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		"addql #1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__(
		"subql #1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	__asm__ __volatile__(
		"subql #1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
	return (*v <= 0);
}

#endif /* __ARCH_M68K_ATOMIC __ */
