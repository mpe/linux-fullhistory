#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#define cpu_logical_map(cpu)	(cpu)

#ifdef __SMP__

#include <linux/tasks.h>

struct cpuinfo_alpha {
	unsigned long loops_per_sec;
	unsigned int next;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};

extern struct cpuinfo_alpha cpu_data[NR_CPUS];

typedef volatile struct {
  unsigned int kernel_flag; /* 4 bytes, please */
  unsigned int akp; /* 4 bytes, please */
  unsigned long pc;
  unsigned int cpu;
} klock_info_t;

extern klock_info_t klock_info;

#define KLOCK_HELD	0xff
#define KLOCK_CLEAR	0x00

extern int task_lock_depth;

#define PROC_CHANGE_PENALTY     20

extern __volatile__ int cpu_number_map[NR_CPUS];

/* HACK: Cabrio WHAMI return value is bogus if more than 8 bits used.. :-( */
#define hard_smp_processor_id() \
({ \
	register unsigned char __r0 __asm__("$0"); \
	__asm__ __volatile__( \
		"call_pal %0" \
		: /* no output (bound to the template) */ \
		:"i" (PAL_whami) \
		:"$0", "$1", "$22", "$23", "$24", "$25", "memory"); \
	__r0; \
})

#define smp_processor_id() hard_smp_processor_id()

#endif /* __SMP__ */

#define NO_PROC_ID	(-1)

#endif
