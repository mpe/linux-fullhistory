/*
 *	linux/arch/alpha/kernel/irq.h
 *
 *	Copyright (C) 1995 Linus Torvalds
 *	Copyright (C) 1998 Richard Henderson
 *
 * This file contains declarations and inline functions for interfacing
 * with the IRQ handling routines in irq.c.
 */


/* FIXME FIXME FIXME FIXME FIXME */
/* We need to figure out why these fail on the DP264 & SX164.  Otherwise
   we'd just do this in init_IRQ().  */
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
