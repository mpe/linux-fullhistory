/* smp.h: Sparc64 specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_SMP_H
#define _SPARC64_SMP_H

#include <linux/tasks.h>
#include <asm/asi.h>

#ifndef __ASSEMBLY__
/* PROM provided per-processor information we need
 * to start them all up.
 */

struct prom_cpuinfo {
	int prom_node;
	int mid;
};

extern int linux_num_cpus;	/* number of CPUs probed  */
extern struct prom_cpuinfo linux_cpus[NR_CPUS];

#endif /* !(__ASSEMBLY__) */

#ifdef __SMP__

#ifndef __ASSEMBLY__

/* Per processor Sparc parameters we need. */

/* Keep this a multiple of 64-bytes for cache reasons. */
struct cpuinfo_sparc {
	/* Dcache line 1 */
	unsigned long	irq_count;
	unsigned int	multiplier;
	unsigned int	counter;
	unsigned long	last_tlbversion_seen;
	unsigned long	pgcache_size;

	/* Dcache line 2 */
	unsigned long	*pgd_cache;
	unsigned long	*pmd_cache;
	unsigned long	*pte_cache;
	unsigned long	udelay_val;
};

extern struct cpuinfo_sparc cpu_data[NR_CPUS];

struct klock_info {
	unsigned char kernel_flag;
	unsigned char akp;
};

extern struct klock_info klock_info;

#define KLOCK_HELD       0xff
#define KLOCK_CLEAR      0x00

/*
 *	Private routines/data
 */
 
extern unsigned char boot_cpu_id;
extern unsigned long cpu_present_map;

/*
 *	General functions that each host system must provide.
 */

extern void smp_callin(void);
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);

extern __volatile__ int cpu_number_map[NR_CPUS];
extern __volatile__ int cpu_logical_map[NR_CPUS];

extern __inline__ int hard_smp_processor_id(void)
{
	unsigned long upaconfig;

	__asm__ __volatile__("ldxa	[%%g0] %1, %0"
			     : "=r" (upaconfig)
			     : "i" (ASI_UPA_CONFIG));
	return ((upaconfig >> 17) & 0x1f);
}

#define smp_processor_id() (current->processor)

#endif /* !(__ASSEMBLY__) */

#define PROC_CHANGE_PENALTY	20

#endif /* !(__SMP__) */

#define NO_PROC_ID		0xFF

#endif /* !(_SPARC64_SMP_H) */
