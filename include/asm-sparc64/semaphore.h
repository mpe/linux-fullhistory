#ifndef _SPARC64_SEMAPHORE_H
#define _SPARC64_SEMAPHORE_H

/* These are actually reasonable on the V9. */

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

extern __inline__ void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern __inline__ int down_interruptible(struct semaphore *sem)
{
	int ret = 0;
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern __inline__ void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	

#endif /* !(_SPARC64_SEMAPHORE_H) */
