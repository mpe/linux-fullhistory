#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#include <linux/linkage.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

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

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

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

extern inline int down_trylock(struct semaphore * sem)
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	register int result __asm__ ("%d0");

	__asm__ __volatile__(
		"| atomic down trylock operation\n\t"
		"subql #1,%1@\n\t"
		"jmi 2f\n\t"
		"clrl %0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra __down_failed_trylock\n"
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
		"addql #1,%0@\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\t"
		"pea 1b\n\t"
		"jbra __up_wakeup\n"
		".previous"
		: /* no outputs */
		: "a" (sem1)
		: "memory");
}

#endif
