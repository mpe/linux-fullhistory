/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_ARM_SEMAPHORE_H
#define __ASM_ARM_SEMAPHORE_H

#include <linux/linkage.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INIT(name,count)			\
	{ ATOMIC_INIT(count), 0,			\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INIT(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count)	\
	struct semaphore name = __SEMAPHORE_INIT(name,count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name)	__DECLARE_SEMAPHORE_GENERIC(name,0)

#define sema_init(sem, val)			\
do {						\
	atomic_set(&((sem)->count), (val));	\
	(sem)->sleepers = 0;			\
	init_waitqueue_head(&(sem)->wait);	\
} while (0)

static inline void init_MUTEX(struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED(struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down_failed (void /* special register calling convention */);
asmlinkage int  __down_interruptible_failed (void /* special register calling convention */);
asmlinkage int  __down_trylock_failed(void  /* params in registers */);
asmlinkage void __up_wakeup (void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern int  __down_trylock(struct semaphore * sem);
extern void __up(struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

#include <asm/proc/semaphore.h>

#endif
