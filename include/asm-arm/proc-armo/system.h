/*
 * linux/include/asm-arm/proc-armo/system.h
 *
 * Copyright (C) 1995, 1996 Russell King
 */

#ifndef __ASM_PROC_SYSTEM_H
#define __ASM_PROC_SYSTEM_H

extern const char xchg_str[];

#include <asm/proc-fns.h>

extern __inline__ unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
	switch (size) {
		case 1:	return processor.u.armv2._xchg_1(x, ptr);
		case 2:	return processor.u.armv2._xchg_2(x, ptr);
		case 4:	return processor.u.armv2._xchg_4(x, ptr);
		default: arm_invalidptr(xchg_str, size);
	}
	return 0;
}

/*
 * We need to turn the caches off before calling the reset vector - RiscOS
 * messes up if we don't
 */
#define proc_hard_reset()	processor._proc_fin()

/*
 * This processor does not idle
 */
#define proc_idle()

/*
 * A couple of speedups for the ARM
 */

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define __save_flags_cli(x)				\
	do {						\
	  unsigned long temp;				\
	  __asm__ __volatile__(				\
"	mov	%0, pc		@ save_flags_cli\n"	\
"	orr	%1, %0, #0x08000000\n"			\
"	and	%0, %0, #0x0c000000\n"			\
"	teqp	%1, #0\n"				\
	  : "=r" (x), "=r" (temp)			\
	  :						\
	  : "memory");					\
	} while (0)
	
/*
 * Enable IRQs
 */
#define __sti()					\
	do {					\
	  unsigned long temp;			\
	  __asm__ __volatile__(			\
"	mov	%0, pc		@ sti\n"	\
"	bic	%0, %0, #0x08000000\n"		\
"	teqp	%0, #0\n"			\
	  : "=r" (temp)				\
	  :					\
	  : "memory");				\
	} while(0)

/*
 * Disable IRQs
 */
#define __cli()					\
	do {					\
	  unsigned long temp;			\
	  __asm__ __volatile__(			\
"	mov	%0, pc		@ cli\n"	\
"	orr	%0, %0, #0x08000000\n"		\
"	teqp	%0, #0\n"			\
	  : "=r" (temp)				\
	  :					\
	  : "memory");				\
	} while(0)

/*
 * save current IRQ & FIQ state
 */
#define __save_flags(x)				\
	do {					\
	  __asm__ __volatile__(			\
"	mov	%0, pc		@ save_flags\n"	\
"	and	%0, %0, #0x0c000000\n"		\
	  : "=r" (x));				\
	} while (0)

/*
 * restore saved IRQ & FIQ state
 */
#define __restore_flags(x)				\
	do {						\
	  unsigned long temp;				\
	  __asm__ __volatile__(				\
"	mov	%0, pc		@ restore_flags\n"	\
"	bic	%0, %0, #0x0c000000\n"			\
"	orr	%0, %0, %1\n"				\
"	teqp	%0, #0\n"				\
	  : "=r" (temp)					\
	  : "r" (x)					\
	  : "memory");					\
	} while (0)

#ifdef __SMP__
#error SMP not supported
#else

#define cli() __cli()
#define sti() __sti()
#define save_flags(x)		__save_flags(x)
#define restore_flags(x)	__restore_flags(x)
#define save_flags_cli(x)	__save_flags_cli(x)

#endif

#endif
