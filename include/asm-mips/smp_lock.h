/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996  Ralf Baechle
 *
 * Linux/MIPS SMP support.  Just a dummy to make building possible.
 */
#ifndef __ASM_MIPS_SMPLOCK_H
#define __ASM_MIPS_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()		do { } while(0)
#define unlock_kernel()		do { } while(0)
#define release_kernel_lock(task, cpu, depth)  ((depth) = 1)
#define reacquire_kernel_lock(task, cpu, depth) do { } while(0)

#else

#error "We do not support SMP on MIPS yet"

#endif /* __SMP__ */

#endif /* __ASM_MIPS_SMPLOCK_H */
