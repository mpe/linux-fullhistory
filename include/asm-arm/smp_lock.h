#ifndef __I386_SMPLOCK_H
#define __I386_SMPLOCK_H

#define __STR(x) #x

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else
#error SMP not supported
#endif /* __SMP__ */

#endif /* __I386_SMPLOCK_H */
