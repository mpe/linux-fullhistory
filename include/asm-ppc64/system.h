#ifndef __PPC64_SYSTEM_H
#define __PPC64_SYSTEM_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/hw_irq.h>
#include <asm/memory.h>

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 * read_barrier_depends() prevents data-dependent loads being reordered
 *	across this point (nop on PPC).
 *
 * We can use the eieio instruction for wmb, but since it doesn't
 * give any ordering guarantees about loads, we have to use the
 * stronger but slower sync instruction for mb and rmb.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("lwsync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("eieio" : : : "memory")
#define read_barrier_depends()  do { } while(0)

#define set_mb(var, value)	do { var = value; mb(); } while (0)
#define set_wmb(var, value)	do { var = value; wmb(); } while (0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()  read_barrier_depends()
#else
#define smp_mb()	__asm__ __volatile__("": : :"memory")
#define smp_rmb()	__asm__ __volatile__("": : :"memory")
#define smp_wmb()	__asm__ __volatile__("": : :"memory")
#define smp_read_barrier_depends()  do { } while(0)
#endif /* CONFIG_SMP */

#ifdef CONFIG_DEBUG_KERNEL
extern void (*debugger)(struct pt_regs *regs);
extern int (*debugger_bpt)(struct pt_regs *regs);
extern int (*debugger_sstep)(struct pt_regs *regs);
extern int (*debugger_iabr_match)(struct pt_regs *regs);
extern int (*debugger_dabr_match)(struct pt_regs *regs);
extern void (*debugger_fault_handler)(struct pt_regs *regs);
#else
#define debugger(regs)			do { } while (0)
#define debugger_bpt(regs)		0
#define debugger_sstep(regs)		0
#define debugger_iabr_match(regs)	0
#define debugger_dabr_match(regs)	0
#define debugger_fault_handler		((void (*)(struct pt_regs *))0)
#endif

#ifdef CONFIG_XMON
extern void xmon_irq(int, void *, struct pt_regs *);

extern void xmon(struct pt_regs *regs);
extern int xmon_bpt(struct pt_regs *regs);
extern int xmon_sstep(struct pt_regs *regs);
extern int xmon_iabr_match(struct pt_regs *regs);
extern int xmon_dabr_match(struct pt_regs *regs);
extern void (*xmon_fault_handler)(struct pt_regs *regs);
#endif

extern void show_regs(struct pt_regs * regs);
extern void flush_instruction_cache(void);
extern int _get_PVR(void);
extern void giveup_fpu(struct task_struct *);
extern void disable_kernel_fp(void);
extern void enable_kernel_fp(void);
extern void giveup_altivec(struct task_struct *);
extern void disable_kernel_altivec(void);
extern void enable_kernel_altivec(void);
extern void cvt_fd(float *from, double *to, unsigned long *fpscr);
extern void cvt_df(double *from, float *to, unsigned long *fpscr);
extern int abs(int);

struct task_struct;
extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);
#define switch_to(prev, next, last)	((last) = __switch_to((prev), (next)))

struct thread_struct;
extern struct task_struct * _switch(struct thread_struct *prev,
				    struct thread_struct *next);

struct pt_regs;
extern void dump_regs(struct pt_regs *);

static inline int __is_processor(unsigned long pv)
{
	unsigned long pvr;
	asm("mfspr %0, 0x11F" : "=r" (pvr)); 
	return(PVR_VER(pvr) == pv);
}

/*
 * Atomic exchange
 *
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 *
 * Inline asm pulled from arch/ppc/kernel/misc.S so ppc64
 * is more like most of the other architectures.
 */
static __inline__ unsigned long
__xchg_u32(volatile int *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	lwarx %0,0,%3		# __xchg_u32\n\
	stwcx. %2,0,%3\n\
2:	bne- 1b"
	ISYNC_ON_SMP
 	: "=&r" (dummy), "=m" (*m)
	: "r" (val), "r" (m)
	: "cc", "memory");

	return (dummy);
}

static __inline__ unsigned long
__xchg_u64(volatile long *m, unsigned long val)
{
	unsigned long dummy;

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx %0,0,%3		# __xchg_u64\n\
	stdcx. %2,0,%3\n\
2:	bne- 1b"
	ISYNC_ON_SMP
	: "=&r" (dummy), "=m" (*m)
	: "r" (val), "r" (m)
	: "cc", "memory");

	return (dummy);
}

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 */
extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__xchg(volatile void *ptr, unsigned long x, int size)
{
	switch (size) {
	case 4:
		return __xchg_u32(ptr, x);
	case 8:
		return __xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x)							     \
  ({									     \
     __typeof__(*(ptr)) _x_ = (x);					     \
     (__typeof__(*(ptr))) __xchg((ptr), (unsigned long)_x_, sizeof(*(ptr))); \
  })

#define tas(ptr) (xchg((ptr),1))

#define __HAVE_ARCH_CMPXCHG	1

static __inline__ unsigned long
__cmpxchg_u32(volatile int *p, int old, int new)
{
	unsigned int prev;

	__asm__ __volatile__ (
	EIEIO_ON_SMP
"1:	lwarx	%0,0,%2		# __cmpxchg_u32\n\
	cmpw	0,%0,%3\n\
	bne-	2f\n\
	stwcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "=m" (*p)
	: "r" (p), "r" (old), "r" (new), "m" (*p)
	: "cc", "memory");

	return prev;
}

static __inline__ unsigned long
__cmpxchg_u64(volatile long *p, unsigned long old, unsigned long new)
{
	unsigned long prev;

	__asm__ __volatile__ (
	EIEIO_ON_SMP
"1:	ldarx	%0,0,%2		# __cmpxchg_u64\n\
	cmpd	0,%0,%3\n\
	bne-	2f\n\
	stdcx.	%4,0,%2\n\
	bne-	1b"
	ISYNC_ON_SMP
	"\n\
2:"
	: "=&r" (prev), "=m" (*p)
	: "r" (p), "r" (old), "r" (new), "m" (*p)
	: "cc", "memory");

	return prev;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	case 8:
		return __cmpxchg_u64(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#endif
