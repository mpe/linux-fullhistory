#ifndef __ASM_MIPS_HARDIRQ_H
#define __ASM_MIPS_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while (0)

#else

#error No habla MIPS SMP

#endif /* __SMP__ */
#endif /* __ASM_MIPS_HARDIRQ_H */
