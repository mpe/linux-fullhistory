#ifndef _PPC_SEMAPHORE_H
#define _PPC_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 * Adapted for PowerPC by Gary Thomas and Paul Mackerras
 */

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	wait_queue_head_t wait;
};

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

#define MUTEX		((struct semaphore) \
			 { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED	((struct semaphore) \
			 { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		ret = __down_interruptible(sem);
	return ret;
}

extern inline int down_trylock(struct semaphore * sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) >= 0)
		wmb();
	else
		ret = __down_trylock(sem);
	return ret;
}

extern inline void up(struct semaphore * sem)
{
	mb();
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	

#endif /* !(_PPC_SEMAPHORE_H) */
