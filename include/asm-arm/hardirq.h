#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>

extern unsigned int local_irq_count[NR_CPUS];
extern unsigned int local_bh_count[NR_CPUS];

#define local_irq_count(cpu)	(local_irq_count[(cpu)])
#define local_bh_count(cpu)	(local_bh_count[(cpu)])

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ const int __cpu = smp_processor_id(); \
	(local_irq_count(__cpu) + local_bh_count(__cpu) != 0); })

#define in_irq() (local_irq_count(smp_processor_id()) != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu,irq)	(local_irq_count(cpu)++)
#define irq_exit(cpu,irq)	(local_irq_count(cpu)--)

#define synchronize_irq()	do { } while (0)

#else
#error SMP not supported
#endif /* CONFIG_SMP */

#endif /* __ASM_HARDIRQ_H */
