/* $Id: irq.h,v 1.4 1998/05/28 03:18:13 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 */
#ifndef __ASM_MIPS_IRQ_H
#define __ASM_MIPS_IRQ_H

/*
 * Actually this is a lie but we hide the local device's interrupts ...
 */
#define NR_IRQS 64

#define TIMER_IRQ 0

extern int (*irq_cannonicalize)(int irq);

struct irqaction;
extern int setup_x86_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

extern unsigned int local_irq_count[];

#ifdef __SMP__
#error Send superfluous SMP boxes to ralf@uni-koblenz.de
#else
#define irq_enter(cpu, irq)     (++local_irq_count[cpu])
#define irq_exit(cpu, irq)      (--local_irq_count[cpu])
#endif

/* Machine specific interrupt initialization  */
extern void (*irq_setup)(void);

#endif /* __ASM_MIPS_IRQ_H */
