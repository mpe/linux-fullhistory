#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

/*
 * Semaphores are recursive: we allow the holder process
 * to recursively do down() operations on a semaphore that
 * the process already owns. In order to do that, we need
 * to keep a semaphore-local copy of the owner and the
 * "depth of ownership".
 *
 * NOTE! Nasty memory ordering rules:
 *  - "owner" and "owner_count" may only be modified once you hold the
 *    lock. 
 *  - "owner_count" must be written _after_ modifying owner, and
 *    must be read _before_ reading owner. There must be appropriate
 *    write and read barriers to enforce this.
 *
 * On an x86, writes are always ordered, so the only enformcement
 * necessary is to make sure that the owner_depth is written after
 * the owner value in program order.
 *
 * For read ordering guarantees, the semaphore wake_lock spinlock
 * is already giving us ordering guarantees.
 *
 * Other (saner) architectures would use "wmb()" and "rmb()" to
 * do this in a more obvious manner.
 */
struct semaphore {
	atomic_t count;
	unsigned long owner, owner_depth;
	int waking;
	struct wait_queue * wait;
};

/*
 * Because we want the non-contention case to be
 * fast, we save the stack pointer into the "owner"
 * field, and to get the true task pointer we have
 * to do the bit masking. That moves the masking
 * operation into the slow path.
 */
#define semaphore_owner(sem) \
	((struct task_struct *)((2*PAGE_MASK) & (sem)->owner))

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, 0, 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, 1, 0, NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

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
	sem->waking++;
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
}

/*
 * NOTE NOTE NOTE!
 *
 * We read owner-count _before_ getting the semaphore. This
 * is important, because the semaphore also acts as a memory
 * ordering point between reading owner_depth and reading
 * the owner.
 *
 * Why is this necessary? The "owner_depth" essentially protects
 * us from using stale owner information - in the case that this
 * process was the previous owner but somebody else is racing to
 * aquire the semaphore, the only way we can see ourselves as an
 * owner is with "owner_depth" of zero (so that we know to avoid
 * the stale value).
 *
 * In the non-race case (where we really _are_ the owner), there
 * is not going to be any question about what owner_depth is.
 *
 * In the race case, the race winner will not even get here, because
 * it will have successfully gotten the semaphore with the locked
 * decrement operation.
 *
 * Basically, we have two values, and we cannot guarantee that either
 * is really up-to-date until we have aquired the semaphore. But we
 * _can_ depend on a ordering between the two values, so we can use
 * one of them to determine whether we can trust the other:
 *
 * Cases:
 *  - owner_depth == zero: ignore the semaphore owner, because it
 *    cannot possibly be us. Somebody else may be in the process
 *    of modifying it and the zero may be "stale", but it sure isn't
 *    going to say that "we" are the owner anyway, so who cares?
 *  - owner_depth is non-zero. That means that even if somebody
 *    else wrote the non-zero count value, the write ordering requriement
 *    means that they will have written themselves as the owner, so
 *    if we now see ourselves as an owner we can trust it to be true.
 */
static inline int waking_non_zero(struct semaphore *sem, struct task_struct *tsk)
{
	unsigned long flags;
	unsigned long owner_depth = sem->owner_depth;
	int ret = 0;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	if (sem->waking > 0 || (owner_depth && semaphore_owner(sem) == tsk)) {
		sem->owner = (unsigned long) tsk;
		sem->owner_depth++;	/* Don't use the possibly stale value */
		sem->waking--;
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/lib/semaphore.S
 */
extern inline void down(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%0)\n\t"
		"js 2f\n\t"
		"movl %%esp,4(%0)\n"
		"movl $1,8(%0)\n\t"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __down_failed\n"
		".previous"
		:/* no outputs */
		:"c" (sem)
		:"memory");
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int result;

	__asm__ __volatile__(
		"# atomic interruptible down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%1)\n\t"
		"js 2f\n\t"
		"movl %%esp,4(%1)\n\t"
		"movl $1,8(%1)\n\t"
		"xorl %0,%0\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __down_failed_interruptible\n"
		".previous"
		:"=a" (result)
		:"c" (sem)
		:"memory");
	return result;
}


/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic up operation\n\t"
		"decl 8(%0)\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"incl 0(%0)\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp __up_wakeup\n"
		".previous"
		:/* no outputs */
		:"c" (sem)
		:"memory");
}

#endif
