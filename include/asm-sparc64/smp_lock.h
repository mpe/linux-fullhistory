/* smp_lock.h: Locking and unlocking the kernel on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SMPLOCK_H
#define __SPARC64_SMPLOCK_H

#include <asm/smp.h>
#include <asm/bitops.h>
#include <asm/pgtable.h>

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu, depth)	((depth) = 1)
#define reaquire_kernel_lock(task, cpu, depth)	do { } while(0)

#else
#error SMP on sparc64 not supported yet
#endif /* (__SMP__) */

#endif /* !(__SPARC64_SMPLOCK_H) */
