/* atops.h: Atomic SPARC operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_ATOPS_H
#define _SPARC_ATOPS_H

#include <linux/config.h>

#ifdef CONFIG_SMP

extern __inline__ __volatile__ unsigned char ldstub(volatile unsigned char *lock)
{
	volatile unsigned char retval;

	__asm__ __volatile__("ldstub [%1], %0\n\t" :
			     "=&r" (retval) :
			     "r" (lock));
	return retval;
}

#endif

#endif
