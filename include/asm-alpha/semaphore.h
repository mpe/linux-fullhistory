#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

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

extern void __down(struct semaphore * sem);
extern void wake_up(struct wait_queue ** p);

/*
 * These are not yet interrupt-safe: should use ldl_l/stl_c here..
 *
 * See include/asm-i386/semaphore.h on how to do this correctly
 * without any jumps or wakeups taken for the no-contention cases.
 */
extern inline void down(struct semaphore * sem)
{
	sem->count--;
	/* "down_failed" */
	if (sem->count < 0)
		__down(sem);
}

extern inline void up(struct semaphore * sem)
{
	sem->count++;
	/* "up_wakeup" */
	__up(sem);
}	

#endif
