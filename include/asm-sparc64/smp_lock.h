/* smp_lock.h: Locking and unlocking the kernel on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SMPLOCK_H
#define __SPARC64_SMPLOCK_H

#include <asm/smp.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>

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
		membar("#LoadStore | #StoreStore");	\
		klock_info.kernel_flag = 0;		\
	}						\
	release_irqlock(cpu);				\
	__sti();					\
} while(0)

/* Do not fuck with this without consulting arch/sparc64/lib/locks.S first! */
#define reacquire_kernel_lock(task, cpu, depth)					\
do {										\
	if(depth) {								\
		register struct klock_info *klip asm("g1");			\
		klip = &klock_info;						\
		__asm__ __volatile__("mov	%%o7, %%g5\n\t"			\
				     "call	___lock_reacquire_kernel\n\t"	\
				     " mov	%1, %%g2"			\
				     : /* No outputs. */			\
				     : "r" (klip), "r" (depth)			\
				     : "g2", "g3", "g5", "memory", "cc");	\
	}									\
} while(0)

/* The following acquire and release the master kernel global lock,
 * the idea is that the usage of this mechanmism becomes less and less
 * as time goes on, to the point where they are no longer needed at all
 * and can thus disappear.
 */

/* Do not fuck with this without consulting arch/sparc64/lib/locks.S first! */
extern __inline__ void lock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	klip = &klock_info;
	__asm__ __volatile__("
	mov	%%o7, %%g5
	call	___lock_kernel
	 lduw	[%%g6 + %0], %%g2
"	: : "i" (AOFF_task_lock_depth), "r" (klip)
	: "g2", "g3", "g5", "memory", "cc");
}

/* Release kernel global lock. */
extern __inline__ void unlock_kernel(void)
{
	register struct klock_info *klip asm("g1");
	klip = &klock_info;
	__asm__ __volatile__("
	mov	%%o7, %%g5
	call	___unlock_kernel
	 lduw	[%%g6 + %0], %%g2
"	: : "i" (AOFF_task_lock_depth), "r" (klip)
	: "g2", "g3", "g5", "memory", "cc");
}

#endif /* (__SMP__) */

#endif /* !(__SPARC64_SMPLOCK_H) */
