#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 */

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { { 1 }, { 0 }, NULL })
#define MUTEX_LOCKED ((struct semaphore) { { 0 }, { 0 }, NULL })

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

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
 * This isn't quite as clever as the x86 side, but the gp register
 * makes things a bit more complicated on the alpha..
 */
extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	

#endif
