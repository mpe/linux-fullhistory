#ifndef _M68K_SEMAPHORE_H
#define _M68K_SEMAPHORE_H

#include <linux/config.h>
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
#ifndef CONFIG_RMW_INSNS
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (atomic_read(&sem->waking) > 0) {
		atomic_dec(&sem->waking);
		ret = 1;
	}
	restore_flags(flags);
#else
	int ret, tmp;

	__asm__ __volatile__
	  ("1:	movel	%2,%0\n"
	   "	jeq	3f\n"
	   "2:	movel	%0,%1\n"
	   "	subql	#1,%1\n"
	   "	casl	%0,%1,%2\n"
	   "	jeq	3f\n"
	   "	tstl	%0\n"
	   "	jne	2b\n"
	   "3:"
	   : "=d" (ret), "=d" (tmp), "=m" (sem->waking));
#endif
	return ret;
}

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/m68k/lib/semaphore.S
 */
extern inline void do_down(struct semaphore * sem, void (*failed)(void))
{
	register struct semaphore *sem1 __asm__ ("%a1") = sem;
	__asm__ __volatile__(
		"| atomic down operation\n\t"
		"subql #1,%0@\n\t"
		"jmi 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		".even\n"
		"2:\tpea 1b\n\t"
		"jbra %1\n"
		".previous"
		: /* no outputs */
		: "a" (sem1), "m" (*(unsigned char *)failed)
		: "memory");
}

#define down(sem) do_down((sem),__down_failed)
#define down_interruptible(sem) do_down((sem),__down_failed_interruptible)

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
		"2:\tpea 1b\n\t"
		"jbra %1\n"
		".previous"
		: /* no outputs */
		: "a" (sem1), "m" (*(unsigned char *)__up_wakeup)
		: "memory");
}

#endif
