/* smp_lock.h: Locking and unlocking the kernel on the Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SMPLOCK_H
#define __SPARC_SMPLOCK_H

#ifdef __SMP__

extern _inline_ unsigned char ldstub(klock_t *lock)
{
	klock_t retval;

	__asm__ __volatile__("ldstub [%1], %0\n\t" :
			     "=r" (retval) :
			     "r" (lock));
	return retval;
}

/* Knock knock... */
extern _inline_ void lock_kernel(void)
{
	unsigned long flags;
	int proc = smp_processor_id();

	save_flags(flags); cli(); /* need this on sparc? */
	while(ldstub(&kernel_lock)) {
		if(proc == active_kernel_processor)
			break;
		if(test_bit(proc, (unsigned long *)&smp_invalidate_needed))
			if(clear_bit(proc, (unsigned long *)&smp_invalidate_needed))
				local_invalidate();
	}
	active_kernel_processor = proc;
	kernel_counter++;
	restore_flags(flags);
}

/* I want out... */
extern _inline_ void unlock_kernel(void)
{
	unsigned long flags;

	save_flags(flags); cli(); /* need this on sparc? */
	if(kernel_counter == 0)
		panic("Bogus kernel counter.\n");
	if(!--kernel_counter) {
		active_kernel_processor = NO_PROC_ID;
		kernel_lock = KLOCK_CLEAR;
	}
	restore_flag(flags);
}

#endif /* !(__SPARC_SMPLOCK_H) */

#endif /* (__SMP__) */
