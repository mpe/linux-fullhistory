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

static __inline__ int waking_non_zero(struct semaphore *sem, struct task_struct *tsk)
{
	int ret;

	__asm__ __volatile__("
1:	ldsw		[%1], %%g5
	brlez,pt	%%g5, 2f
	 mov		0, %0
	sub		%%g5, 1, %%g7
	cas		[%1], %%g5, %%g7
	cmp		%%g5, %%g7
	bne,pn		%%icc, 1b
	 mov		1, %0
2:"	: "=r" (ret)
	: "r" (&((sem)->waking))
	: "g5", "g7", "cc", "memory");
	return ret;
}

extern __inline__ void down(struct semaphore * sem)
{
	int result;

	result = atomic_dec_return(&sem->count);
	membar("#StoreLoad | #StoreStore");
	if (result < 0)
		__down(sem);
}

extern __inline__ int down_interruptible(struct semaphore *sem)
{
	int result, ret = 0;

	result = atomic_dec_return(&sem->count);
	membar("#StoreLoad | #StoreStore");
	if (result < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern __inline__ void up(struct semaphore * sem)
{
	membar("#StoreStore | #LoadStore");
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}	

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_SEMAPHORE_H) */
