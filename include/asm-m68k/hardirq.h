#ifndef __M68K_HARDIRQ_H
#define __M68K_HARDIRQ_H

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#define hardirq_trylock(cpu)	((cpu)==0)	/* always true */
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#endif
#ifndef __M68K_HARDIRQ_H
#define __M68K_HARDIRQ_H

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#define hardirq_trylock(cpu)	((cpu)==0)	/* always true */
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#endif
