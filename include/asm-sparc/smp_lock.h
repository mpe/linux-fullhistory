/* smp_lock.h: Locking and unlocking the kernel on the Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SMPLOCK_H
#define __SPARC_SMPLOCK_H

#include <asm/smp.h>
#include <asm/bitops.h>
#include <asm/atops.h>
#include <asm/pgtable.h>

#ifdef __SMP__

/*
 *	Locking the kernel 
 */

/* Knock knock... */
extern __inline void lock_kernel(void)
{
	unsigned long flags;
	int proc = smp_processor_id();

	save_flags(flags); cli(); /* need this on sparc? */
	while(ldstub(&kernel_flag)) {
		if(proc == active_kernel_processor)
			break;
		do {
#ifdef __SMP_PROF__		
			smp_spins[smp_processor_id()]++;
#endif			
			barrier();
		} while(kernel_flag); /* Don't lock the bus more than we have to. */
	}
	active_kernel_processor = proc;
	kernel_counter++;
	restore_flags(flags);
}

/* I want out... */
extern __inline void unlock_kernel(void)
{
	unsigned long flags;

	save_flags(flags); cli(); /* need this on sparc? */
	if(kernel_counter == 0)
		panic("Bogus kernel counter.\n");

	if(!--kernel_counter) {
		active_kernel_processor = NO_PROC_ID;
		kernel_flag = KLOCK_CLEAR;
	}
	restore_flags(flags);
}

#endif /* !(__SPARC_SMPLOCK_H) */

#endif /* (__SMP__) */
