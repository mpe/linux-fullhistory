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
#define lock_kernel()           do { } while(0)
#define unlock_kernel()         do { } while(0)
#else

extern __inline__ __volatile__ unsigned char ldstub(volatile unsigned char *lock)
{
	volatile unsigned char retval;

	__asm__ __volatile__("ldstub [%1], %0" : "=&r" (retval) : "r" (lock));
	return retval;
}

/*
 *	Locking the kernel 
 */

/* Knock knock... */
extern __inline__ void lock_kernel(void)
{
	int proc = smp_processor_id();

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
}

/* I want out... */
extern __inline__ void unlock_kernel(void)
{
	if(kernel_counter == 0)
		panic("Bogus kernel counter.\n");

	if(!--kernel_counter) {
		active_kernel_processor = NO_PROC_ID;
		kernel_flag = KLOCK_CLEAR;
	}
}

#endif /* (__SMP__) */

#endif /* !(__SPARC64_SMPLOCK_H) */
