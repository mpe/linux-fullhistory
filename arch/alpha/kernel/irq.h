/*
 *	linux/arch/alpha/kernel/irq.h
 *
 *	Copyright (C) 1995 Linus Torvalds
 *	Copyright (C) 1998 Richard Henderson
 *
 * This file contains declarations and inline functions for interfacing
 * with the IRQ handling routines in irq.c.
 */

#include <linux/config.h>

#define STANDARD_INIT_IRQ_PROLOG	\
	outb(0, DMA1_RESET_REG);	\
	outb(0, DMA2_RESET_REG);	\
	outb(0, DMA1_CLR_MASK_REG);	\
	outb(0, DMA2_CLR_MASK_REG)

extern unsigned long alpha_irq_mask;

extern void generic_ack_irq(unsigned long irq);
extern void isa_device_interrupt(unsigned long vector, struct pt_regs * regs);
extern void srm_device_interrupt(unsigned long vector, struct pt_regs * regs);

extern void handle_irq(int irq, int ack, struct pt_regs * regs);

#define RTC_IRQ    8
#ifdef CONFIG_RTC
#define TIMER_IRQ  0			 /* timer is the pit */
#else
#define TIMER_IRQ  RTC_IRQ		 /* timer is the rtc */
#endif

