#ifndef _I386_SEMAPHORE_HELPER_H
#define _I386_SEMAPHORE_HELPER_H

/*
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1999 Andrea Arcangeli
 */

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * but on the x86 we need an external synchronizer.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (atomic_read(&sem->count) <= 0)
		sem->waking++;
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 *
 * We must undo the sem->count down_interruptible() increment while we are
 * protected by the spinlock in order to make atomic this atomic_inc() with the
 * atomic_read() in wake_one_more(), otherwise we can race. -arca
 */
static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	} else if (signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 *
 * We must undo the sem->count down_trylock() increment while we are
 * protected by the spinlock in order to make atomic this atomic_inc() with the
 * atomic_read() in wake_one_more(), otherwise we can race. -arca
 */
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking <= 0)
		atomic_inc(&sem->count);
	else {
		sem->waking--;
		ret = 0;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

#endif
