#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { 1, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { 0, 0, NULL })

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

/* This isn't quite as clever as the x86 side, I'll be fixing this
 * soon enough.
 */
extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	if(atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	

#endif /* !(_SPARC_SEMAPHORE_H) */
