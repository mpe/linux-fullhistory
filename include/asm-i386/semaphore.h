#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 * Modified 1999-02-14 by Andrea Arcangeli, split the sched.c helper
 *		       functions in asm/sempahore-helper.h while fixing a
 *		       potential and subtle race discovered by Ulrich Schmid
 *		       in down_interruptible(). Since I started to play here I
 *		       also implemented the `trylock' semaphore operation.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

struct semaphore {
	atomic_t count;
	int waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/lib/semaphore.S
 */
extern inline void down(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%0)\n\t"
		"js 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __down_failed\n"
		".previous"
		:/* no outputs */
		:"c" (sem)
		:"memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%1)\n\t"
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __down_failed_interruptible\n"
		".previous"
		:"=a" (result)
		:"c" (sem)
		:"memory");
	return result;
}

extern inline int down_trylock(struct semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%1)\n\t"
		"js 2f\n\t"
		"xorl %0,%0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __down_failed_trylock\n"
		".previous"
		:"=a" (result)
		:"c" (sem)
		:"memory");
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
	__asm__ __volatile__(
		"# atomic up operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"incl 0(%0)\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __up_wakeup\n"
		".previous"
		:/* no outputs */
		:"c" (sem)
		:"memory");
}

#endif
