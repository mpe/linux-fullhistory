#ifndef _ASM_IA64_HARDIRQ_H
#define _ASM_IA64_HARDIRQ_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>
#include <linux/threads.h>

extern unsigned int local_irq_count[NR_CPUS];
extern unsigned long hardirq_no[NR_CPUS];

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */

#define in_interrupt()						\
({								\
	int __cpu = smp_processor_id();				\
	(local_irq_count[__cpu] + local_bh_count[__cpu]) != 0;	\
})

#ifndef CONFIG_SMP
# define hardirq_trylock(cpu)		(local_irq_count[cpu] == 0)
# define hardirq_endlock(cpu)		((void) 0)

# define hardirq_enter(cpu, irq)	(local_irq_count[cpu]++)
# define hardirq_exit(cpu, irq)		(local_irq_count[cpu]--)

# define synchronize_irq()		barrier()
#else

#include <linux/spinlock.h>

#include <asm/atomic.h>
#include <asm/smp.h>

extern int global_irq_holder;
extern spinlock_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == cpu) {
		global_irq_holder = NO_PROC_ID;
		spin_unlock(&global_irq_lock);
        }
}

static inline void hardirq_enter(int cpu, int irq)
{
	++local_irq_count[cpu];
        atomic_inc(&global_irq_count);
}

static inline void hardirq_exit(int cpu, int irq)
{
	atomic_dec(&global_irq_count);
        --local_irq_count[cpu];
}

static inline int hardirq_trylock(int cpu)
{
	return !local_irq_count[cpu] && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)  ((void)0)

extern void synchronize_irq(void);

#endif /* CONFIG_SMP */
#endif /* _ASM_IA64_HARDIRQ_H */
