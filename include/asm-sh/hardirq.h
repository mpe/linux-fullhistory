#ifndef __ASM_SH_HARDIRQ_H
#define __ASM_SH_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>

extern unsigned int __local_irq_count[NR_CPUS];
extern unsigned int __local_bh_count[NR_CPUS];

#define local_irq_count(cpu) (__local_irq_count[(cpu)])
#define local_bh_count(cpu) (__local_bh_count[(cpu)])

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(__local_irq_count[__cpu] + __local_bh_count[__cpu] != 0); })

#define in_irq() (__local_irq_count[smp_processor_id()] != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(__local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu, irq)	(__local_irq_count[cpu]++)
#define irq_exit(cpu, irq)	(__local_irq_count[cpu]--)

#define synchronize_irq()	barrier()

#else

#error Super-H SMP is not available

#endif /* CONFIG_SMP */
#endif /* __ASM_SH_HARDIRQ_H */
