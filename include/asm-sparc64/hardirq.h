/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while(0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while(0)

#else /* (__SMP__) */

#include <asm/atomic.h>
#include <asm/spinlock.h>
#include <asm/system.h>
#include <asm/smp.h>

extern unsigned char global_irq_holder;
extern spinlock_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore... */
	if(global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		spin_unlock(&global_irq_lock);
	}
}

static inline void hardirq_enter(int cpu)
{
	++local_irq_count[cpu];
	atomic_inc(&global_irq_count);
}

static inline void hardirq_exit(int cpu)
{
	atomic_dec(&global_irq_count);
	--local_irq_count[cpu];
}

static inline int hardirq_trylock(int cpu)
{
	unsigned long flags;

	__save_and_cli(flags);
	if(atomic_add_return(1, &global_irq_count) != 1 ||
	   *(((unsigned char *)(&global_irq_lock)))) {
		atomic_dec(&global_irq_count);
		__restore_flags(flags);
		return 0;
	}
	++local_irq_count[cpu];
	return 1;
}

static inline void hardirq_endlock(int cpu)
{
	__cli();
	hardirq_exit(cpu);
	__sti();
}

extern void synchronize_irq(void);

#endif /* __SMP__ */

#endif /* !(__SPARC64_HARDIRQ_H) */
