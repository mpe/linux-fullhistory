/* $Id: irq.h,v 1.1 1996/12/26 17:28:13 davem Exp $
 * irq.h: IRQ registers on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

/* XXX Write the sun5 specific code... XXX */

#ifndef _SPARC64_IRQ_H
#define _SPARC64_IRQ_H

#include <linux/linkage.h>

#include <asm/system.h>     /* For NCPUS */

#define NR_IRQS    15

/* Dave Redman (djhr@tadpole.co.uk)
 * changed these to function pointers.. it saves cycles and will allow
 * the irq dependencies to be split into different files at a later date
 * sun4c_irq.c, sun4m_irq.c etc so we could reduce the kernel size.
 */
extern void (*disable_irq)(unsigned int);
extern void (*enable_irq)(unsigned int);
extern void (*clear_clock_irq)( void );
extern void (*clear_profile_irq)( void );
extern void (*load_profile_irq)( unsigned int timeout );
extern void (*init_timers)(void (*lvl10_irq)(int, void *, struct pt_regs *));
extern void claim_ticker14(void (*irq_handler)(int, void *, struct pt_regs *),
			   int irq,
			   unsigned int timeout);

#ifdef __SMP__
extern void (*set_cpu_int)(int, int);
extern void (*clear_cpu_int)(int, int);
extern void (*set_irq_udt)(int);
#endif

extern int request_fast_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), unsigned long flags, __const__ char *devname);

#endif
