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

#define set_cr(x)					\
	do {						\
	__asm__ __volatile__(				\
	"mcr	p15, 0, %0, c1, c0	@ set CR"	\
	  : : "r" (x));					\
	} while (0)

extern unsigned long cr_no_alignment;	/* defined in entry-armv.S */
extern unsigned long cr_alignment;	/* defined in entry-armv.S */

/*
 * We can wait for an interrupt...
 */
#define proc_idle()						\
	do {							\
	__asm__ __volatile__(					\
"	mcr	p15, 0, %0, c15, c8, 2	@ proc_idle"		\
	  : : "r" (0));						\
	} while (0)

/*
 * A couple of speedups for the ARM
 */

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define __save_flags_cli(x)					\
	do {							\
	  unsigned long temp;					\
	  __asm__ __volatile__(					\
	"mrs	%1, cpsr		@ save_flags_cli\n"	\
"	and	%0, %1, #192\n"					\
"	orr	%1, %1, #128\n"					\
"	msr	cpsr, %1"					\
	  : "=r" (x), "=r" (temp)				\
	  :							\
	  : "memory");						\
	} while (0)
	
/*
 * Enable IRQs
 */
#define __sti()							\
	do {							\
	  unsigned long temp;					\
	  __asm__ __volatile__(					\
	"mrs	%0, cpsr		@ sti\n"		\
"	bic	%0, %0, #128\n"					\
"	msr	cpsr, %0"					\
	  : "=r" (temp)						\
	  :							\
	  : "memory");						\
	} while(0)

/*
 * Disable IRQs
 */
#define __cli()							\
	do {							\
	  unsigned long temp;					\
	  __asm__ __volatile__(					\
	"mrs	%0, cpsr		@ cli\n"		\
"	orr	%0, %0, #128\n"					\
"	msr	cpsr, %0"					\
	  : "=r" (temp)						\
	  :							\
	  : "memory");						\
	} while(0)

/*
 * save current IRQ & FIQ state
 */
#define __save_flags(x)						\
	do {							\
	  __asm__ __volatile__(					\
	"mrs	%0, cpsr		@ save_flags\n"		\
"	and	%0, %0, #192"					\
	  : "=r" (x)						\
	  :							\
	  : "memory");						\
	} while (0)

/*
 * restore saved IRQ & FIQ state
 */
#define __restore_flags(x)					\
	do {							\
	  unsigned long temp;					\
	  __asm__ __volatile__(					\
	"mrs	%0, cpsr		@ restore_flags\n"	\
"	bic	%0, %0, #192\n"					\
"	orr	%0, %0, %1\n"					\
"	msr	cpsr, %0"					\
	  : "=r" (temp)						\
	  : "r" (x)						\
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
