#ifndef _ASM_IA64_HARDIRQ_H
#define _ASM_IA64_HARDIRQ_H

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __local_irq_count;
	unsigned int __local_bh_count;
	unsigned int __nmi_counter;
# if NR_CPUS > 1
	unsigned int __pad[13];		/* this assumes 64-byte cache-lines... */
# endif
} ____cacheline_aligned irq_cpustat_t;

extern irq_cpustat_t irq_stat[NR_CPUS];

/*
 * Simple wrappers reducing source bloat
 */
#define local_irq_count(cpu)	(irq_stat[(cpu)].__local_irq_count)
#define local_bh_count(cpu)	(irq_stat[(cpu)].__local_bh_count)
#define nmi_counter(cpu)	(irq_stat[(cpu)].__nmi_counter)

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt()						\
({								\
	int __cpu = smp_processor_id();				\
	(local_irq_count(__cpu) + local_bh_count(__cpu)) != 0;	\
})

#define in_irq() (local_irq_count(smp_processor_id()) != 0)

#ifndef CONFIG_SMP
# define hardirq_trylock(cpu)		(local_irq_count(cpu) == 0)
# define hardirq_endlock(cpu)		do { } while (0)

# define irq_enter(cpu, irq)		(++local_irq_count(cpu))
# define irq_exit(cpu, irq)		(--local_irq_count(cpu))

# define synchronize_irq()		barrier()
#else

#include <asm/atomic.h>
#include <asm/smp.h>

extern unsigned int global_irq_holder;
extern volatile unsigned int global_irq_lock;

static inline int irqs_running (void)
{
	int i;

	for (i = 0; i < smp_num_cpus; i++)
		if (local_irq_count(i))
			return 1;
	return 0;
}

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == cpu) {
		global_irq_holder = NO_PROC_ID;
		clear_bit(0,&global_irq_lock);
        }
}

static inline void irq_enter(int cpu, int irq)
{
	++local_irq_count(cpu);

	while (test_bit(0,&global_irq_lock)) {
		/* nothing */;
	}
}

static inline void irq_exit(int cpu, int irq)
{
	--local_irq_count(cpu);
}

static inline int hardirq_trylock(int cpu)
{
	return !local_irq_count(cpu) && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */
#endif /* _ASM_IA64_HARDIRQ_H */
