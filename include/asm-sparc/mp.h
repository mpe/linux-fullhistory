/* $Id: mp.h,v 1.2 1995/11/25 02:32:06 davem Exp $
 * mp.h:  Multiprocessing definitions for the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_MP_H
#define _SPARC_MP_H

#include <asm/traps.h>
#include <asm/page.h>
#include <asm/vaddrs.h>

extern int linux_smp_still_initting;

struct sparc_percpu {
	struct tt_entry trap_table[NUM_SPARC_TRAPS];  /* One page */
	unsigned int kernel_stack[PAGE_SIZE/4];       /* One page */
	int cpuid;          /* Who am I? */
	int cpu_is_alive;   /* Linux has fired it up. */
	int cpu_is_idling;  /* Is sitting in the idle loop. */
	/* More to come... */
	char filler[PERCPU_ENTSIZE-(PAGE_SIZE*2)-0xc];
};

extern struct sparc_percpu *percpu_table;

struct prom_cpuinfo {
	int prom_node;
	int mid;
};

extern struct prom_cpuinfo linux_cpus[NCPUS];

#endif /* !(_SPARC_MP_H) */
