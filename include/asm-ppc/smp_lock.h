#ifndef __PPC_SMPLOCK_H
#define __PPC_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()		do { } while (0)
#define unlock_kernel()		do { } while (0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else /* __SMP__ */

/* Release global kernel lock and global interrupt lock */
#define release_kernel_lock(task, cpu, depth)		\
do {							\
	if((depth = (task)->lock_depth) != 0) {		\
		__cli();				\
		(task)->lock_depth = 0;			\
		klock_info.akp = NO_PROC_ID;		\
		klock_info.kernel_flag = 0;		\
	} 						\
	release_irqlock(cpu);				\
	__sti();					\
} while(0)

extern void reacquire_kernel_lock(struct task_struct *, int,int);

/* The following acquire and release the master kernel global lock,
 * the idea is that the usage of this mechanmism becomes less and less
 * as time goes on, to the point where they are no longer needed at all
 * and can thus disappear.
 */
extern void __lock_kernel(struct task_struct *);
extern void __unlock_kernel(struct task_struct *);

extern __inline__ void lock_kernel(void)
{
	__lock_kernel(current);
}

/* Release kernel global lock. */
extern __inline__ void unlock_kernel(void)
{
	__unlock_kernel(current);
}


#endif /* __SMP__ */
#endif /* __PPC_SMPLOCK_H */
