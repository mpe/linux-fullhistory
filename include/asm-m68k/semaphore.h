#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * m68k version by Andreas Schwab
 */

struct semaphore {
	int count;
	int waiting;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { 0, 0, NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern void __up(struct semaphore * sem);

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
		"subql #1,%0\n\t"
		"jmi " SYMBOL_NAME_STR(__down_failed) "\n"
		"1:"
		: /* no outputs */
		: "m" (sem->count), "a" (sem1)
		: "%a0", "memory");
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
