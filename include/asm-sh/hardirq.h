#ifndef __ASM_SH_HARDIRQ_H
#define __ASM_SH_HARDIRQ_H

#include <linux/threads.h>

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#define in_irq() (local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu, irq)	(local_irq_count[cpu]++)
#define irq_exit(cpu, irq)	(local_irq_count[cpu]--)

#define synchronize_irq()	barrier()

#else

#error Super-H SMP is not available

#endif /* __SMP__ */
#endif /* __ASM_SH_HARDIRQ_H */
