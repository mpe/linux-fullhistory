#ifndef __ARCH_ALPHA_ATOMIC__
#define __ARCH_ALPHA_ATOMIC__

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc...
 *
 * But use these as seldom as possible since they are much more slower
 * than regular operations.
 */

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(struct { int a[100]; } *)x)

typedef int atomic_t;

extern __inline__ void atomic_add(atomic_t i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"addl %0,%2,%0\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));
}

extern __inline__ void atomic_sub(atomic_t i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"subl %0,%2,%0\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (__atomic_fool_gcc(v))
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));
}

/*
 * Same as above, but return true if we counted down to zero
 */
extern __inline__ int atomic_sub_and_test(atomic_t i, atomic_t * v)
{
	unsigned long temp, result;
	__asm__ __volatile__(
		"\n1:\t"
		"ldl_l %0,%1\n\t"
		"subl %0,%3,%0\n\t"
		"bis %0,%0,%2\n\t"
		"stl_c %0,%1\n\t"
		"beq %0,1b\n"
		"2:"
		:"=&r" (temp),
		 "=m" (__atomic_fool_gcc(v)),
		 "=&r" (result)
		:"Ir" (i),
		 "m" (__atomic_fool_gcc(v)));
	return result==0;
}

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))
#define atomic_dec_and_test(v) atomic_sub_and_test(1,(v))

#endif
