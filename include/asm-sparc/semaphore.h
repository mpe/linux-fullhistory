#ifndef _SPARC_SEMAPHORE_H
#define _SPARC_SEMAPHORE_H

/* Dinky, good for nothing, just barely irq safe, Sparc semaphores. */

#ifdef __KERNEL__

#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	atomic_t waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), ATOMIC_INIT(0), NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), ATOMIC_INIT(0), NULL })

extern void __down(struct semaphore * sem);
extern int __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

#define wake_one_more(sem)	atomic_inc(&sem->waking);

/* XXX Put this in raw assembler for SMP case so that the atomic_t
 * XXX spinlock can allow this to be done without grabbing the IRQ
 * XXX global lock.
 */
#define waking_non_zero(sem) \
({	unsigned long flags; \
	int ret = 0; \
	save_flags(flags); \
	cli(); \
	if (atomic_read(&sem->waking) > 0) { \
		atomic_dec(&sem->waking); \
		ret = 1; \
	} \
	restore_flags(flags); \
	ret; \
})

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

#endif /* __KERNEL__ */

#endif /* !(_SPARC_SEMAPHORE_H) */
