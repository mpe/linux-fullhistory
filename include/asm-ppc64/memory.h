#ifndef _ASM_PPC64_MEMORY_H_ 
#define _ASM_PPC64_MEMORY_H_ 

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

/*
 * Arguably the bitops and *xchg operations don't imply any memory barrier
 * or SMP ordering, but in fact a lot of drivers expect them to imply
 * both, since they do on x86 cpus.
 */
#ifdef CONFIG_SMP
#define EIEIO_ON_SMP	"eieio\n"
#define ISYNC_ON_SMP	"\n\tisync"
#else
#define EIEIO_ON_SMP
#define ISYNC_ON_SMP
#endif

static inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}

#ifdef CONFIG_SMP
#define eieio_on_smp()	eieio()
#define isync_on_smp()	isync()
#else
#define eieio_on_smp()	__asm__ __volatile__("": : :"memory")
#define isync_on_smp()	__asm__ __volatile__("": : :"memory")
#endif

/* Macros for adjusting thread priority (hardware multi-threading) */

#if defined(CONFIG_PPC_ISERIES) || defined(CONFIG_HMT)
#define HMT_low()	asm volatile("or 1,1,1		# low priority")
#define HMT_medium()	asm volatile("or 2,2,2		# medium priority")
#define HMT_high()	asm volatile("or 3,3,3		# high priority")

#define HMT_LOW		"\tor	1,1,1		# low priority\n"
#define HMT_MEDIUM	"\tor	2,2,2		# medium priority\n"
#define HMT_HIGH	"\tor	3,3,3		# high priority\n"
#else
#define HMT_low()	do { } while(0)
#define HMT_medium()	do { } while(0)
#define HMT_high()	do { } while(0)

#define HMT_LOW
#define HMT_MEDIUM
#define HMT_HIGH
#endif

#endif
