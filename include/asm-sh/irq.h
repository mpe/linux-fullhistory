#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

/*
 *
 * linux/include/asm-sh/irq.h
 *
 * Copyright (C) 1999  Niibe Yutaka
 *
 */

#include <linux/config.h>

#define TIMER_IRQ		16	/* Hard-wired */
#define TIMER_IPR_OFFSET	12
#define TIMER_PRIORITY		 2

#if defined(__SH4__)
/*
 * 48 = 32+16
 *
 * 32 for on chip support modules.
 * 16 for external interrupts.
 *
 */
#define NR_IRQS	48
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
#define NR_IRQS 32
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
#define NR_IRQS 61
#endif

extern void disable_irq(unsigned int);
extern void disable_irq_nosync(unsigned int);
extern void enable_irq(unsigned int);

/*
 * Function for "on chip support modules".
 */
extern void set_ipr_data(unsigned int irq, int offset, int priority);
extern void make_onChip_irq(unsigned int irq);
extern void make_imask_irq(unsigned int irq);

#endif /* __ASM_SH_IRQ_H */
