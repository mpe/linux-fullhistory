/* smp_lock.h: 32-bit Sparc SMP master lock primitives.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SMPLOCK_H
#define __SPARC_SMPLOCK_H

#include <asm/smp.h>
#include <asm/ptrace.h>

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else

#include <asm/hardirq.h>

/* Release global kernel lock and global interrupt lock */
#define release_kernel_lock(task, cpu, depth)		\
do {							\
	if((depth = (task)->lock_depth) != 0) {		\
		__cli();				\
		(task)->lock_depth = 0;			\
		klock_info.akp = NO_PROC_ID;		\
		klock_info.kernel_flag = 0;		\
	}						\
	release_irqlock(cpu);				\
	__sti();					\
} while(0)

/* Do not fuck with this without consulting arch/sparc/lib/locks.S first! */
#define reacquire_kernel_lock(task, cpu, depth)					\
do {										\
	if(depth) {								\
		register struct klock_info *klip asm("g1");			\
		register int proc asm("g5");					\
		klip = &klock_info;						\
		proc = cpu;							\
		__asm__ __volatile__("mov	%%o7, %%g4\n\t"			\
				     "call	___lock_reacquire_kernel\n\t"	\
				     " mov	%2, %%g2"			\
				     : /* No outputs. */			\
				     : "r" (klip), "r" (proc), "r" (depth)	\
				     : "g2", "g3", "g4", "g7", "memory", "cc");	\
	}									\
} while(0)

/* The following acquire and release the master kernel global lock,
 * the idea is that the usage of this mechanmism becomes less and less
 * as time goes on, to the point where they are no longer needed at all
 * and can thus disappear.
 */

/* Do not fuck with this without consulting arch/sparc/lib/locks.S first! */
extern __inline__ void lock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	register int proc asm("g5");
	klip = &klock_info;
	proc = smp_processor_id();

#if 1 /* Debugging */
	if(local_irq_count[proc]) {
		__label__ l1;
l1:		printk("lock from interrupt context at %p\n", &&l1);
	}
	if(proc == global_irq_holder) {
		__label__ l2;
l2:		printk("Ugh at %p\n", &&l2);
		sti();
	}
#endif

	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___lock_kernel
	 ld	[%%g6 + %0], %%g2
"	: : "i" (AOFF_task_lock_depth), "r" (klip), "r" (proc)
	: "g2", "g3", "g4", "g7", "memory", "cc");
}

/* Release kernel global lock. */
extern __inline__ void unlock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	klip = &klock_info;
	__asm__ __volatile__("
	mov	%%o7, %%g4
	call	___unlock_kernel
	 ld	[%%g6 + %0], %%g2
"	: : "i" (AOFF_task_lock_depth), "r" (klip)
	: "g2", "g3", "g4", "memory", "cc");
}

#endif /* !(__SPARC_SMPLOCK_H) */

#endif /* (__SMP__) */
