#ifndef __M68K_HARDIRQ_H
#define __M68K_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];

#define in_interrupt() (local_irq_count[smp_processor_id()] + local_bh_count[smp_processor_id()] != 0)

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	barrier()

#endif
