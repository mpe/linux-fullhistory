#ifndef __ALPHA_SMPLOCK_H
#define __ALPHA_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while (0)

#else

#include <asm/system.h>
#include <asm/current.h>
#include <asm/bitops.h>
#include <asm/hardirq.h>

#define kernel_lock_held() \
  (klock_info.kernel_flag && (klock_info.akp == smp_processor_id()))

/* Release global kernel lock and global interrupt lock */
#define release_kernel_lock(task, cpu, depth)		\
do {							\
	if ((depth = (task)->lock_depth) != 0) {	\
		__cli();				\
		(task)->lock_depth = 0;			\
		klock_info.akp = NO_PROC_ID;		\
		klock_info.kernel_flag = 0;		\
		mb();					\
	}						\
	release_irqlock(cpu);				\
	__sti();					\
} while (0)

#if 1
#define DEBUG_KERNEL_LOCK
#else
#undef DEBUG_KERNEL_LOCK
#endif

#ifdef DEBUG_KERNEL_LOCK
extern void ___lock_kernel(klock_info_t *klip, int cpu, long ipl);
#else /* DEBUG_KERNEL_LOCK */
static inline void ___lock_kernel(klock_info_t *klip, int cpu, long ipl)
{
	long regx;

	__asm__ __volatile__(
	"1:	ldl_l	%1,%0;"
	"	blbs	%1,6f;"
	"	or	%1,1,%1;"
	"	stl_c	%1,%0;"
	"	beq	%1,6f;"
	"4:	mb\n"
	".section .text2,\"ax\"\n"
	"6:	mov %4,$16;"
	"	call_pal %3;"
	"7:	ldl %1,%0;"
	"	blbs %1,7b;"
	"	bis $31,7,$16;"
	"	call_pal %3;"
	"	br 1b\n"
	".previous"
	: "=m,=m" (__dummy_lock(klip)), "=&r,=&r" (regx)
	: "0,0" (__dummy_lock(klip)), "i,i" (PAL_swpipl), "i,r" (ipl)
	: "$0", "$1", "$16", "$22", "$23", "$24", "$25", "memory"
	);
}
#endif /* DEBUG_KERNEL_LOCK */

#define reacquire_kernel_lock(task, cpu, depth)	\
do {						\
  if (depth) {					\
    long ipl;					\
    klock_info_t *klip = &klock_info;		\
    __save_and_cli(ipl);			\
    ___lock_kernel(klip, cpu, ipl);		\
    klip->akp = cpu;				\
    (task)->lock_depth = depth;			\
    __restore_flags(ipl);			\
  }						\
} while (0)

/* The following acquire and release the master kernel global lock,
 * the idea is that the usage of this mechanmism becomes less and less
 * as time goes on, to the point where they are no longer needed at all
 * and can thus disappear.
 */

#define lock_kernel()				\
if (current->lock_depth > 0) {			\
     ++current->lock_depth;			\
} else {					\
  long ipl;					\
  int cpu = smp_processor_id();			\
  klock_info_t *klip = &klock_info;		\
  __save_and_cli(ipl);				\
  ___lock_kernel(klip, cpu, ipl);		\
  klip->akp = cpu;				\
  current->lock_depth = 1;			\
  __restore_flags(ipl);				\
}

/* Release kernel global lock. */
#define unlock_kernel()				\
if (current->lock_depth > 1) {			\
  --current->lock_depth;			\
} else {					\
  long ipl;					\
  __save_and_cli(ipl);				\
  klock_info.akp = NO_PROC_ID;			\
  klock_info.kernel_flag = KLOCK_CLEAR;		\
  mb();						\
  current->lock_depth = 0;			\
  __restore_flags(ipl);				\
}  

#endif /* __SMP__ */

#endif /* __ALPHA_SMPLOCK_H */
