#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifdef __SMP__

#include <linux/tasks.h>
#include <asm/pal.h>

struct cpuinfo_alpha {
	unsigned long loops_per_sec;
	unsigned int next;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
	unsigned long ipi_count;
} __attribute__((aligned(32)));

extern struct cpuinfo_alpha cpu_data[NR_CPUS];

#define PROC_CHANGE_PENALTY     20

extern __volatile__ int cpu_number_map[NR_CPUS];

/* HACK: Cabrio WHAMI return value is bogus if more than 8 bits used.. :-( */

static __inline__ unsigned char hard_smp_processor_id(void)
{
	register unsigned char __r0 __asm__("$0");
	__asm__ __volatile__(
		"call_pal %1 #whami"
		: "=r"(__r0)
		:"i" (PAL_whami)
		: "$1", "$22", "$23", "$24", "$25");
	return __r0;
}

#define smp_processor_id()	(current->processor)
#define cpu_logical_map(cpu)	(cpu)

#endif /* __SMP__ */

#define NO_PROC_ID	(-1)

#endif
