/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995 - 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#define NR_IRQS 64

#define TIMER_IRQ 0

extern int (*irq_cannonicalize)(int irq);

struct irqaction;
extern int i8259_setup_irq(int irq, struct irqaction * new);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

/* Machine specific interrupt initialization  */
extern void (*irq_setup)(void);

#endif /* _ASM_IRQ_H */
