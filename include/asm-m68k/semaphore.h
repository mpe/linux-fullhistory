#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#include <linux/linkage.h>
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
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { { 1 }, { 0 }, NULL })
#define MUTEX_LOCKED ((struct semaphore) { { 0 }, { 0 }, NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
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
		"lea %%pc@(1f),%%a0\n\t"
		"subql #1,%0@\n\t"
		"jmi " SYMBOL_NAME_STR(__down_failed) "\n"
		"1:"
		: /* no outputs */
		: "a" (sem1)
		: "%a0", "memory");
}

/*
 * This version waits in interruptible state so that the waiting
 * process can be killed.  The down_failed_interruptible routine
 * returns negative for signalled and zero for semaphore acquired.
 */
extern inline int down_interruptible(struct semaphore * sem)
{
	register int ret __asm__ ("%d0");
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	__asm__ __volatile__(
		"| atomic interruptible down operation\n\t"
		"lea %%pc@(1f),%%a0\n\t"
		"subql #1,%1@\n\t"
		"jmi " SYMBOL_NAME_STR(__down_failed_interruptible) "\n\t"
		"clrl %0\n"
		"1:"
		: "=d" (ret)
		: "a" (sem1)
		: "%d0", "%a0", "memory");
	return ret;
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
		"lea %%pc@(1f),%%a0\n\t"
		"addql #1,%0\n\t"
		"jle " SYMBOL_NAME_STR(__up_wakeup) "\n"
		"1:"
		: /* no outputs */
		: "m" (sem->count), "a" (sem1)
		: "%a0", "memory");
}

#endif
