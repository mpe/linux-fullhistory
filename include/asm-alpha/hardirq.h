#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()  (local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	((cpu) == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#else

#error FIXME

#endif /* __SMP__ */
#endif /* _ALPHA_HARDIRQ_H */
