/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)
#define hardirq_depth()	(local_irq_count[smp_processor_id()])

#ifdef __SMP__
#error SMP not supported on sparc64
#else /* !(__SMP__) */

#define hardirq_trylock(cpu)	(++local_irq_count[cpu], (cpu)==0)
#define hardirq_endlock(cpu)	(--local_irq_count[cpu])

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while(0)

#endif /* __SMP__ */

#endif /* !(__SPARC64_HARDIRQ_H) */
