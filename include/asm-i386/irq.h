#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds
 *
 *	IRQ/IPI changes taken from work by Thomas Radke <tomsoft@informatik.tu-chemnitz.de>
 */

#include <linux/linkage.h>
#include <asm/segment.h>

#define NR_IRQS 16

#define TIMER_IRQ 0

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif /* _ASM_IRQ_H */
