#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/threads.h>

extern unsigned int local_irq_count[NR_CPUS];

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ const int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] + local_bh_count[__cpu] != 0); })

#define in_irq() (local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while (0)

#else
#error SMP not supported
#endif /* __SMP__ */

#endif /* __ASM_HARDIRQ_H */
