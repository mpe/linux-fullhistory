/* $Id: irq.h,v 1.10 1998/05/29 06:00:39 ecd Exp $
 * irq.h: IRQ registers on the 64-bit Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
 */

#ifndef _SPARC64_IRQ_H
#define _SPARC64_IRQ_H

#include <linux/linkage.h>
#include <linux/kernel.h>

struct devid_cookie {
	int dummy;
};

/* You should not mess with this directly. That's the job of irq.c. */
struct ino_bucket {
	unsigned short ino;
	short imap_off;
	unsigned short pil;
	unsigned short flags;
	unsigned int *iclr;
};

#define __irq_ino(irq) ((struct ino_bucket *)(unsigned long)(irq))->ino
#define __irq_pil(irq) ((struct ino_bucket *)(unsigned long)(irq))->pil

static __inline__ char *__irq_itoa(unsigned int irq)
{
	static char buff[16];

	sprintf(buff, "%d,%x", __irq_pil(irq), __irq_ino(irq));
	return buff;
}

#define NR_IRQS    15

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);
extern void init_timers(void (*lvl10_irq)(int, void *, struct pt_regs *),
			unsigned long *);
extern unsigned int build_irq(int pil, int inofixup, unsigned int *iclr, unsigned int *imap);
extern unsigned int sbus_build_irq(void *sbus, unsigned int ino);
extern unsigned int psycho_build_irq(void *psycho, int imap_off, int ino, int need_dma_sync);

#ifdef __SMP__
extern void set_cpu_int(int, int);
extern void clear_cpu_int(int, int);
extern void set_irq_udt(int);
#endif

extern int request_fast_irq(unsigned int irq,
			    void (*handler)(int, void *, struct pt_regs *),
			    unsigned long flags, __const__ char *devname,
			    void *dev_id);

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
