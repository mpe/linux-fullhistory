/*
 * SMP- and interrupt-safe semaphores..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1996  Linus Torvalds, Ralf Baechle
 */
#ifndef __ASM_MIPS_SEMAPHORE_H
#define __ASM_MIPS_SEMAPHORE_H

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/spinlock.h>

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

extern spinlock_t semaphore_wake_lock;

#define sema_init(sem, val)	atomic_set(&((sem)->count), val)

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * which we have.  Let the rest of the losers suck eggs.
 */

static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc(&sem->waking);
}

static inline int waking_non_zero(struct semaphore *sem, struct task_struct *tsk)
{
	int ret, tmp;

	__asm__ __volatile__(
	"1:\tll\t%1,%2\n"
	"blez\t%1,2f\n\t"
	"subu\t%0,%1,1\n\t"
	"sc\t%0,%2\n\t"
	"beqz\t%0,1b\n\t"
	"2:"
	".text"
	: "=r"(ret), "=r"(tmp), "=m"(__atomic_fool_gcc(&sem->waking))
	: "0"(0));

	return ret;
}

extern inline void down(struct semaphore * sem)
{
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

extern inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
extern inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

#endif /* __ASM_MIPS_SEMAPHORE_H */
