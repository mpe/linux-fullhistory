#ifndef _SPARC64_SEMAPHORE_H
#define _SPARC64_SEMAPHORE_H

/* These are actually reasonable on the V9. */
#ifdef __KERNEL__

#include <asm/atomic.h>
#include <asm/system.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

#define wake_one_more(sem)      atomic_inc(&sem->waking);

extern __inline__ int waking_non_zero(struct semaphore *sem)
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

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_SEMAPHORE_H) */
