#ifndef __M68K_SMPLOCK_H
#define __M68K_SMPLOCK_H

/*
 * We don't do SMP so this is again one of these silly dummy files
 * to keep the kernel source looking nice ;-(.
 */

#define lock_kernel()		do { } while(0)
#define unlock_kernel()		do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth)	do { } while(0)

#endif
