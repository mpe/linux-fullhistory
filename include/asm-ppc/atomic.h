/*
 * PowerPC atomic operations
 */

#ifndef _ASM_PPC_ATOMIC_H_ 
#define _ASM_PPC_ATOMIC_H_

#ifdef __SMP__
typedef struct { volatile int counter; } atomic_t;
#else
typedef struct { int counter; } atomic_t;
#endif

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

extern void atomic_add(int a, atomic_t *v);
extern int  atomic_add_return(int a, atomic_t *v);
extern void atomic_sub(int a, atomic_t *v);
extern void atomic_inc(atomic_t *v);
extern int  atomic_inc_return(atomic_t *v);
extern void atomic_dec(atomic_t *v);
extern int  atomic_dec_return(atomic_t *v);
extern int  atomic_dec_and_test(atomic_t *v);

extern void atomic_clear_mask(unsigned long mask, unsigned long *addr);
extern void atomic_set_mask(unsigned long mask, unsigned long *addr);

#if 0	/* for now */
extern __inline__ void atomic_add(atomic_t a, atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3\n\
	add	%0,%2,%0\n\
	stwcx.	%0,0,%3\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (a), "r" (v)
	: "cc");
}

extern __inline__ void atomic_sub(atomic_t a, atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3\n\
	subf	%0,%2,%0\n\
	stwcx.	%0,0,%3\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (a), "r" (v)
	: "cc");
}

extern __inline__ int atomic_sub_and_test(atomic_t a, atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%3\n\
	subf	%0,%2,%0\n\
	stwcx.	%0,0,%3\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (a), "r" (v)
	: "cc");

	return t == 0;
}

extern __inline__ void atomic_inc(atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2\n\
	addic	%0,%0,1\n\
	stwcx.	%0,0,%2\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (v)
	: "cc");
}

extern __inline__ void atomic_dec(atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2\n\
	addic	%0,%0,-1\n\
	stwcx.	%0,0,%2\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (v)
	: "cc");
}

extern __inline__ int atomic_dec_and_test(atomic_t *v)
{
	atomic_t t;

	__asm__ __volatile__("\n\
1:	lwarx	%0,0,%2\n\
	addic	%0,%0,-1\n\
	stwcx.	%0,0,%2\n\
	bne	1b"
	: "=&r" (t), "=m" (*v)
	: "r" (v)
	: "cc");

	return t == 0;
}
#endif /* 0 */

#endif /* _ASM_PPC_ATOMIC_H_ */
