/*
 * include/asm-mips/irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH
 * written by Ralf Baechle
 *
 */
#ifndef __ASM_MIPS_IRQ_H
#define __ASM_MIPS_IRQ_H

/*
 * Actually this is a lie but we hide the local device's interrupts ...
 */
#define NR_IRQS 64

#define TIMER_IRQ 0

struct irqaction;
extern int setup_x86_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif /* __ASM_MIPS_IRQ_H */
