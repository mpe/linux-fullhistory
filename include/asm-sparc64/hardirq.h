/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/tasks.h>

#ifndef __SMP__
extern unsigned int local_irq_count;
#else
#define local_irq_count		(cpu_data[smp_processor_id()].irq_count)
#endif

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ((local_irq_count + local_bh_count) != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count == 0)
#define hardirq_endlock(cpu)	do { } while(0)

#define hardirq_enter(cpu)	(local_irq_count++)
#define hardirq_exit(cpu)	(local_irq_count--)

#define synchronize_irq()	barrier()

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
	++(cpu_data[cpu].irq_count);
	atomic_inc(&global_irq_count);
	membar("#StoreLoad | #StoreStore");
}

static inline void hardirq_exit(int cpu)
{
	membar("#StoreStore | #LoadStore");
	atomic_dec(&global_irq_count);
	--(cpu_data[cpu].irq_count);
}

static inline int hardirq_trylock(int cpu)
{
	return (! atomic_read(&global_irq_count) &&
		! spin_is_locked (&global_irq_lock));
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* __SMP__ */

#endif /* !(__SPARC64_HARDIRQ_H) */
