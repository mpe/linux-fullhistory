#ifndef __PPC_SMPLOCK_H
#define __PPC_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()		do { } while (0)
#define unlock_kernel()		do { } while (0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else

#error need to defined lock_kernel and unlock_kernel, etc.

#endif /* __SMP__ */
#endif /* __PPC_SMPLOCK_H */
