#ifndef _ALPHA_ATOMIC_H
#define _ALPHA_ATOMIC_H

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc...
 *
 * But use these as seldom as possible since they are much slower
 * than regular operations.
 */

#ifdef __SMP__
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	( (atomic_t) { (i) } )

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		((v)->counter = (i))

/*
 * Make sure gcc doesn't try to be clever and move things around
 * on us. We need to use _exactly_ the address the user gave us,
 * not some alias that contains the same information.
 */
#define __atomic_fool_gcc(x) (*(struct { int a[100]; } *)x)

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

extern __inline__ void atomic_add(int i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	addl %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (__atomic_fool_gcc(v))
	:"Ir" (i), "m" (__atomic_fool_gcc(v)));
}

extern __inline__ void atomic_sub(int i, atomic_t * v)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	subl %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (__atomic_fool_gcc(v))
	:"Ir" (i), "m" (__atomic_fool_gcc(v)));
}

/*
 * Same as above, but return the result value
 */
extern __inline__ long atomic_add_return(int i, atomic_t * v)
{
	long temp, result;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	addl %0,%3,%0\n"
	"	mov %0,%2\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (__atomic_fool_gcc(v)), "=&r" (result)
	:"Ir" (i), "m" (__atomic_fool_gcc(v)));
	return result;
}

extern __inline__ long atomic_sub_return(int i, atomic_t * v)
{
	long temp, result;
	__asm__ __volatile__(
	"1:	ldl_l %0,%1\n"
	"	subl %0,%3,%0\n"
	"	mov %0,%2\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (__atomic_fool_gcc(v)), "=&r" (result)
	:"Ir" (i), "m" (__atomic_fool_gcc(v)));
	return result;
}

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic_inc_return(v) atomic_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic_dec(v) atomic_sub(1,(v))

#endif /* _ALPHA_ATOMIC_H */
