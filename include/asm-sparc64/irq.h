/* $Id: irq.h,v 1.6 1997/08/07 08:06:40 davem Exp $
 * irq.h: IRQ registers on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_IRQ_H
#define _SPARC64_IRQ_H

#include <linux/linkage.h>

/* Sparc64 extensions to the interrupt registry flags.  Mostly this is
 * for passing along what bus type the device is on and also the true
 * format of the dev_id cookie, see below.
 */
#define SA_BUSMASK	0x0f000
#define SA_SBUS		0x01000
#define SA_PCI		0x02000
#define SA_FHC		0x03000
#define SA_EBUS		0x04000
#define SA_BUS(mask)	((mask) & SA_BUSMASK)

struct devid_cookie {
	/* Caller specifies these. */
	void		*real_dev_id;		/* What dev_id would usually contain. */
	unsigned int	*imap;			/* Anonymous IMAP register */
	unsigned int	*iclr;			/* Anonymous ICLR register */
	int		pil;			/* Anonymous PIL */
	void		*bus_cookie;		/* SYSIO regs, PSYCHO regs, etc. */

	/* Return values. */
	unsigned int	ret_ino;
	unsigned int	ret_pil;
};

#define SA_DCOOKIE	0x10000

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
