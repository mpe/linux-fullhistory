/* $Id: irq.h,v 1.4 1997/04/04 00:50:20 davem Exp $
 * irq.h: IRQ registers on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_IRQ_H
#define _SPARC64_IRQ_H

#include <linux/linkage.h>

#include <asm/system.h>     /* For NCPUS */

#define NR_IRQS    15

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);
extern void init_timers(void (*lvl10_irq)(int, void *, struct pt_regs *));

#ifdef __SMP__
extern void set_cpu_int(int, int);
extern void clear_cpu_int(int, int);
extern void set_irq_udt(int);
#endif

extern int request_fast_irq(unsigned int irq,
			    void (*handler)(int, void *, struct pt_regs *),
			    unsigned long flags, __const__ char *devname);

extern __inline__ void set_softint(unsigned long bits)
{
	__asm__ __volatile__("wr	%0, 0x0, %%set_softint"
			     : /* No outputs */
			     : "r" (bits));
}

extern __inline__ void clear_softint(unsigned long bits)
{
	__asm__ __volatile__("wr	%0, 0x0, %%clear_softint"
			     : /* No outputs */
			     : "r" (bits));
}

extern __inline__ unsigned long get_softint(void)
{
	unsigned long retval;

	__asm__ __volatile__("rd	%%softint, %0"
			     : "=r" (retval));
	return retval;
}

#endif
