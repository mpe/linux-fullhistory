#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

/*
 *
 * linux/include/asm-sh/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

#define TIMER_IRQ		16	/* Hard-wired */
#define TIMER_IRP_OFFSET	12
#define TIMER_PRIORITY		 1

/*
 * 48 = 32+16
 *
 * 32 for on chip support modules.
 * 16 for external interrupts.
 *
 */
#define NR_IRQS	48

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * Function for "on chip support modules".
 */
extern void set_ipr_data(unsigned int irq, int offset, int priority);
extern void make_onChip_irq(unsigned int irq);

#endif /* __ASM_SH_IRQ_H */
