#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu)		do { } while(0)
#define reacquire_kernel_lock(task)		do { } while(0)

#else

#include <linux/interrupt.h>
#include <asm/spinlock.h>

extern spinlock_t kernel_flag;

/*
 * Release global kernel lock and global interrupt lock
 */
#define release_kernel_lock(task, cpu) \
do { \
	if (task->lock_depth) \
		spin_unlock(&kernel_flag); \
	release_irqlock(cpu); \
	__sti(); \
} while (0)

/*
 * Re-acquire the kernel lock
 */
#define reacquire_kernel_lock(task) \
do { \
	if (task->lock_depth) \
		spin_lock(&kernel_flag); \
} while (0)


/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously,
 * so we only need to worry about other
 * CPU's.
 */
extern __inline__ void lock_kernel(void)
{
	struct task_struct *tsk = current;
	int lock_depth;

	lock_depth = tsk->lock_depth;
	tsk->lock_depth = lock_depth+1;
	if (!lock_depth)
		spin_lock(&kernel_flag);
}

extern __inline__ void unlock_kernel(void)
{
	struct task_struct *tsk = current;
	int lock_depth;

	lock_depth = tsk->lock_depth-1;
	tsk->lock_depth = lock_depth;
	if (!lock_depth)
		spin_unlock(&kernel_flag);
}

#endif /* __SMP__ */

#endif
