/* smp.h: PPC specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#ifdef __SMP__

#ifndef __ASSEMBLY__

extern struct prom_cpuinfo linux_cpus[NCPUS];

/* Per processor PPC parameters we need. */

struct cpuinfo_PPC {
	unsigned long udelay_val; /* that's it */
};

extern struct cpuinfo_PPC cpu_data[NR_CPUS];
#endif /* __ASSEMBLY__ */

#endif /* !(__SMP__) */

#endif /* !(_PPC_SMP_H) */
