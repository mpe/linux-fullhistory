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
#define __atomic_fool_gcc(x) (*(volatile struct { int a[100]; } *)x)

#ifdef __SMP__
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

static __inline__ void atomic_add(int i, volatile atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "addl %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_sub(int i, volatile atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "subl %1,%0"
		:"=m" (__atomic_fool_gcc(v))
		:"ir" (i), "m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_inc(volatile atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "incl %0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ void atomic_dec(volatile atomic_t *v)
{
	__asm__ __volatile__(
		LOCK "decl %0"
		:"=m" (__atomic_fool_gcc(v))
		:"m" (__atomic_fool_gcc(v)));
}

static __inline__ int atomic_dec_and_test(volatile atomic_t *v)
{
	unsigned char c;

	__asm__ __volatile__(
		LOCK "decl %0; sete %1"
		:"=m" (__atomic_fool_gcc(v)), "=qm" (c)
		:"m" (__atomic_fool_gcc(v)));
	return c != 0;
}

/* These are x86-specific, used by some header files */
#define atomic_clear_mask(mask, addr) \
__asm__ __volatile__(LOCK "andl %0,%1" \
: : "r" (~(mask)),"m" (__atomic_fool_gcc(addr)) : "memory")

#define atomic_set_mask(mask, addr) \
__asm__ __volatile__(LOCK "orl %0,%1" \
: : "r" (mask),"m" (__atomic_fool_gcc(addr)) : "memory")

#endif
