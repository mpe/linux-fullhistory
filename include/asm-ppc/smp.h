/* smp.h: PPC specific SMP stuff.
 *
 * Taken from asm-sparc/smp.h
 */

#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/kernel.h> /* for panic */
#include <linux/tasks.h> /* for NR_CPUS */

#ifdef __SMP__

#ifndef __ASSEMBLY__

extern unsigned long cpu_present_map;

/* per processor PPC parameters we need. */
struct cpuinfo_PPC {
	unsigned long loops_per_sec;
	unsigned long pvr;
	unsigned long *pgd_quick;
	unsigned long *pte_quick;
	unsigned long pgtable_cache_sz;
};

extern struct cpuinfo_PPC cpu_data[NR_CPUS];

struct klock_info_struct {
	unsigned long kernel_flag;
	unsigned char akp;
};

extern struct klock_info_struct klock_info;

#define KLOCK_HELD       0xffffffff
#define KLOCK_CLEAR      0x0

#define PROC_CHANGE_PENALTY     20

extern __volatile__ int cpu_number_map[NR_CPUS];
extern __volatile__ int __cpu_logical_map[NR_CPUS];
extern unsigned long smp_proc_in_lock[NR_CPUS];

extern __inline__ int cpu_logical_map(int cpu)
{
	return __cpu_logical_map[cpu];
}

extern __inline__ int hard_smp_processor_id(void)
{
	int cpuid = 0;
	/* assume cpu # 0 for now */
	return cpuid;
}

#define smp_processor_id() (current->processor)

extern void smp_message_pass(int target, int msg, unsigned long data, int wait);

#endif /* __ASSEMBLY__ */

#else /* !(__SMP__) */
#ifndef __ASSEMBLY__
extern __inline__ int cpu_logical_map(int cpu)
{
	return cpu;
}
#endif
#endif /* !(__SMP__) */

#define NO_PROC_ID               0xFF            /* No processor magic marker */

extern void smp_store_cpu_info(int id);

#endif /* !(_PPC_SMP_H) */
