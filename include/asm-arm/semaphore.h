/*
 * linux/include/asm-arm/semaphore.h
 */
#ifndef __ASM_ARM_SEMAPHORE_H
#define __ASM_ARM_SEMAPHORE_H

#include <linux/linkage.h>
#include <asm/system.h>
#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, NULL })

asmlinkage void __down_failed (void /* special register calling convention */);
asmlinkage int  __down_interruptible_failed (void /* special register calling convention */);
asmlinkage void __up_wakeup (void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern int  __down_interruptible(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * but on the x86 we need an external synchronizer.
 * Currently this is just the global interrupt lock,
 * bah. Go for a smaller spinlock some day.
 *
 * (On the other hand this shouldn't be in any critical
 * path, so..)
 */
static inline void wake_one_more(struct semaphore * sem)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	sem->waking++;
	restore_flags(flags);
}

static inline int waking_non_zero(struct semaphore *sem, struct task_struct *tsk)
{
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	}
	restore_flags(flags);
	return ret;
}

#include <asm/proc/semaphore.h>

#endif
