#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#define TIMER_IRQ 0

/*
 * 16 XT IRQ's, 8 potential APIC interrupt sources.
 * Right now the APIC is only used for SMP, but this
 * may change.
 */
#define NR_IRQS 64

static __inline__ int irq_cannonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#endif /* _ASM_IRQ_H */
