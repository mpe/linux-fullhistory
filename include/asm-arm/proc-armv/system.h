/*
 * linux/include/asm-arm/proc-armv/system.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_PROC_SYSTEM_H
#define __ASM_PROC_SYSTEM_H

extern const char xchg_str[];

extern __inline__ unsigned long __xchg(unsigned long x, volatile void *ptr, int size)
{
	switch (size) {
		case 1:	__asm__ __volatile__ ("swpb %0, %1, [%2]" : "=r" (x) : "r" (x), "r" (ptr) : "memory");
			break;
		case 2:	abort ();
		case 4:	__asm__ __volatile__ ("swp %0, %1, [%2]" : "=r" (x) : "r" (x), "r" (ptr) : "memory");
			break;
		default: arm_invalidptr(xchg_str, size);
	}
	return x;
}

/*
 * This processor does not need anything special before reset,
 * but RPC may do...
 */
extern __inline__ void proc_hard_reset(void)
{
}

/*
 * We can wait for an interrupt...
 */
#define proc_idle()			\
	do {				\
	__asm__ __volatile__(		\
"	mcr	p15, 0, %0, c15, c8, 2"	\
	  : : "r" (0));			\
	} while (0)

/*
 * A couple of speedups for the ARM
 */

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define __save_flags_cli(x)		\
	do {				\
	  unsigned long temp;		\
	  __asm__ __volatile__(		\
	"mrs	%1, cpsr\n"		\
"	and	%0, %1, #192\n"		\
"	orr	%1, %1, #128\n"		\
"	msr	cpsr, %1"		\
	  : "=r" (x), "=r" (temp)	\
	  :				\
	  : "memory");			\
	} while (0)
	
/*
 * Enable IRQs
 */
#define __sti()				\
	do {				\
	  unsigned long temp;		\
	  __asm__ __volatile__(		\
	"mrs	%0, cpsr\n"		\
"	bic	%0, %0, #128\n"		\
"	msr	cpsr, %0"		\
	  : "=r" (temp)			\
	  :				\
	  : "memory");			\
	} while(0)

/*
 * Disable IRQs
 */
#define __cli()				\
	do {				\
	  unsigned long temp;		\
	  __asm__ __volatile__(		\
	"mrs	%0, cpsr\n"		\
"	orr	%0, %0, #128\n"		\
"	msr	cpsr, %0"		\
	  : "=r" (temp)			\
	  :				\
	  : "memory");			\
	} while(0)

/*
 * save current IRQ & FIQ state
 */
#define __save_flags(x)			\
	do {				\
	  __asm__ __volatile__(		\
	"mrs	%0, cpsr\n"		\
"	and	%0, %0, #192"		\
	  : "=r" (x)			\
	  :				\
	  : "memory");			\
	} while (0)

/*
 * restore saved IRQ & FIQ state
 */
#define __restore_flags(x)		\
	do {				\
	  unsigned long temp;		\
	  __asm__ __volatile__(		\
	"mrs	%0, cpsr\n"		\
"	bic	%0, %0, #192\n"		\
"	orr	%0, %0, %1\n"		\
"	msr	cpsr, %0"		\
	  : "=r" (temp)			\
	  : "r" (x)			\
	  : "memory");			\
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
