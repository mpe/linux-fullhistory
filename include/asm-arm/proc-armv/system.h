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
	extern void arm_invalidptr(const char *, int);

	switch (size) {
		case 1:	__asm__ __volatile__ ("swpb %0, %1, [%2]" : "=r" (x) : "r" (x), "r" (ptr) : "memory");
			break;
		case 4:	__asm__ __volatile__ ("swp %0, %1, [%2]" : "=r" (x) : "r" (x), "r" (ptr) : "memory");
			break;
		default: arm_invalidptr(xchg_str, size);
	}
	return x;
}

#define set_cr(x)					\
	__asm__ __volatile__(				\
	"mcr	p15, 0, %0, c1, c0	@ set CR"	\
	: : "r" (x))

extern unsigned long cr_no_alignment;	/* defined in entry-armv.S */
extern unsigned long cr_alignment;	/* defined in entry-armv.S */

/*
 * A couple of speedups for the ARM
 */

/*
 * Save the current interrupt enable state & disable IRQs
 */
#define __save_flags_cli(x)					\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ save_flags_cli\n"	\
"	orr	%1, %0, #128\n"					\
"	msr	cpsr_c, %1"					\
	: "=r" (x), "=r" (temp)					\
	:							\
	: "memory");						\
	})
	
/*
 * Enable IRQs
 */
#define __sti()							\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ sti\n"		\
"	bic	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory");						\
	})

/*
 * Disable IRQs
 */
#define __cli()							\
	({							\
		unsigned long temp;				\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ cli\n"		\
"	orr	%0, %0, #128\n"					\
"	msr	cpsr_c, %0"					\
	: "=r" (temp)						\
	:							\
	: "memory");						\
	})

/*
 * save current IRQ & FIQ state
 */
#define __save_flags(x)						\
	__asm__ __volatile__(					\
	"mrs	%0, cpsr		@ save_flags\n"		\
	  : "=r" (x)						\
	  :							\
	  : "memory")

/*
 * restore saved IRQ & FIQ state
 */
#define __restore_flags(x)					\
	__asm__ __volatile__(					\
	"msr	cpsr_c, %0		@ restore_flags\n"	\
	:							\
	: "r" (x)						\
	: "memory")

/* For spinlocks etc */
#define local_irq_save(x)	__save_flags_cli(x)
#define local_irq_restore(x)	__restore_flags(x)
#define local_irq_disable()	__cli()
#define local_irq_enable()	__sti()

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
