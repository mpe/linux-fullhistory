#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 */

struct semaphore {
	int count;
	int waiting;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { 0, 0, NULL })

asmlinkage void down_failed(void /* special register calling convention */);
asmlinkage void up_wakeup(void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/lib/semaphore.S
 */
extern inline void down(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic down operation\n"
		"1:\n\t"
		"leal 1b,%%eax\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl %0\n\t"
		"js " SYMBOL_NAME_STR(down_failed)
		:/* no outputs */
		:"m" (sem->count), "c" (sem)
		:"ax","dx","memory");
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
		"leal 1f,%%eax\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"incl %0\n\t"
		"jle " SYMBOL_NAME_STR(up_wakeup)
		"\n1:"
		:/* no outputs */
		:"m" (sem->count), "c" (sem)
		:"ax", "dx", "memory");
}

#endif
