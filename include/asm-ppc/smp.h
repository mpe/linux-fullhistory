/* smp.h: PPC specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/kernel.h> /* for panic */

#ifdef __SMP__

#ifndef __ASSEMBLY__

extern unsigned long cpu_present_map;

/* per processor PPC parameters we need. */
struct cpuinfo_PPC {
	unsigned long udelay_val;
};

extern struct cpuinfo_PPC cpu_data[NR_CPUS];

struct klock_info {
	unsigned char kernel_flag;
	unsigned char akp;
};

extern struct klock_info klock_info;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

#define PROC_CHANGE_PENALTY     20

extern __volatile__ int cpu_number_map[NR_CPUS];
extern __volatile__ int cpu_logical_map[NR_CPUS];
extern unsigned long smp_proc_in_lock[NR_CPUS];

extern __inline__ int hard_smp_processor_id(void)
{
	int cpuid;
	if ( ! have_of() ) /* assume prep */
		panic("hard_smp_processor_id()\n");
	else
		panic("hard_smp_processor_id()\n");

	return cpuid;
}

#define smp_processor_id() hard_smp_processor_id()

#endif /* __ASSEMBLY__ */

#endif /* !(__SMP__) */

#define NO_PROC_ID               0xFF            /* No processor magic marker */

#endif /* !(_PPC_SMP_H) */
