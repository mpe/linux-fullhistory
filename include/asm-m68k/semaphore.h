#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/current.h>
#include <asm/system.h>
#include <asm/atomic.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * m68k version by Andreas Schwab
 */

struct semaphore {
	atomic_t count;
	unsigned long owner, owner_depth;
	atomic_t waking;
	struct wait_queue * wait;
};

/*
 * Because we want the non-contention case to be
 * fast, we save the stack pointer into the "owner"
 * field, and to get the true task pointer we have
 * to do the bit masking. That moves the masking
 * operation into the slow path.
 */
#define semaphore_owner(sem) \
	((struct task_struct *)((2*PAGE_MASK) & (sem)->owner))

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, 0, ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, 1, ATOMIC_INIT(0), NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem, struct task_struct *tsk)
{
#ifndef CONFIG_RMW_INSNS
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (atomic_read(&sem->waking) > 0 || (owner_depth && semaphore_owner(sem) == tsk)) {
		sem->owner = (unsigned long)tsk;
		sem->owner_depth++;
		atomic_dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
#else
	int ret, tmp;

	__asm__ __volatile__
	  ("1:	movel	%2,%0\n"
	   "    jeq	3f\n"
	   "2:	movel	%0,%1\n"
	   "	subql	#1,%1\n"
	   "	casl	%0,%1,%2\n"
	   "	jeq	3f\n"
	   "	tstl	%0\n"
	   "	jne	2b\n"
	   "3:"
	   : "=d" (ret), "=d" (tmp), "=m" (sem->waking));

	ret |= ((sem->owner_depth != 0) && (semaphore_owner(sem) == tsk));
	if (ret) {
		sem->owner = (unsigned long)tsk;
		sem->owner_depth++;
	}

#endif
	return ret;
}

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/m68k/lib/semaphore.S
 */
extern inline void down(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	__asm__ __volatile__(
		"| atomic down operation\n\t"
		"subql #1,%0@\n\t"
		"jmi 2f\n\t"
		"movel %%sp,4(%0)\n"
		"movel #1,8(%0)\n\t"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed\n"
		".previous"
		: /* no outputs */
		: "a" (sem1)
		: "memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	register int result __asm__ ("%d0");

	__asm__ __volatile__(
		"| atomic interruptible down operation\n\t"
		"subql #1,%1@\n\t"
		"jmi 2f\n\t"
		"movel %%sp,4(%1)\n"
		"moveql #1,%0\n"
		"movel %0,8(%1)\n"
		"clrl %0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed_interruptible\n"
		".previous"
		: "=d" (result)
		: "a" (sem1)
		: "%d0", "memory");
	return result;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	__asm__ __volatile__(
		"| atomic up operation\n\t"
		"subql #1,8(%0)\n\t"
		"addql #1,%0@\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __up_wakeup\n"
		".previous"
		: /* no outputs */
		: "a" (sem1)
		: "memory");
}

#endif
