/* $Id: irq.h,v 1.8 1995/11/25 02:31:54 davem Exp $
 * irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/linkage.h>

#include <asm/system.h>     /* For NCPUS */

#define NR_IRQS    15

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

extern int request_fast_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname);

/* On the sun4m, just like the timers, we have both per-cpu and master
 * interrupt registers.
 */

/* These registers are used for sending/receiving irqs from/to
 * different cpu's.   
 */
struct sun4m_intreg_percpu {
	unsigned int tbt;        /* Interrupts still pending for this cpu. */

	/* These next two registers are WRITE-ONLY and are only
	 * "on bit" sensitive, "off bits" written have NO affect.
	 */
	unsigned int clear;  /* Clear this cpus irqs here. */
	unsigned int set;    /* Set this cpus irqs here. */
	unsigned char space[PAGE_SIZE - 12];
};

struct sun4m_intregs {
	struct sun4m_intreg_percpu cpu_intregs[NCPUS];
	unsigned int tbt;                /* IRQ's that are still pending. */
	unsigned int irqs;               /* Master IRQ bits. */

	/* Again, like the above, two these registers are WRITE-ONLY. */
	unsigned int clear;              /* Clear master IRQ's by setting bits here. */
	unsigned int set;                /* Set master IRQ's by setting bits here. */

	/* This register is both READ and WRITE. */
	unsigned int undirected_target;  /* Which cpu gets undirected irqs. */
};

extern struct sun4m_intregs *sun4m_interrupts;

/* Bit field defines for the interrupt registers on various
 * Sparc machines.
 */

/* The sun4c interrupt register. */
#define SUN4C_INT_ENABLE  0x01     /* Allow interrupts. */
#define SUN4C_INT_E14     0x80     /* Enable level 14 IRQ. */
#define SUN4C_INT_E10     0x20     /* Enable level 10 IRQ. */
#define SUN4C_INT_E8      0x10     /* Enable level 8 IRQ. */
#define SUN4C_INT_E6      0x08     /* Enable level 6 IRQ. */
#define SUN4C_INT_E4      0x04     /* Enable level 4 IRQ. */
#define SUN4C_INT_E1      0x02     /* Enable level 1 IRQ. */

/* The sun4m interrupt registers.  MUST RESEARCH THESE SOME MORE XXX */
#define SUN4M_INT_ENABLE  0x80000000
#define SUN4M_INT_E14     0x00000080
#define SUN4M_INT_E10     0x00080000

/* XXX add cross-cpu ipi functions XXX */

#endif
