/* hardirq.h: 32-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-99 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef __SPARC_HARDIRQ_H
#define __SPARC_HARDIRQ_H

#include <linux/threads.h>

#ifndef __SMP__
extern unsigned int local_irq_count;

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt()  ((local_irq_count + local_bh_count) != 0)

#define hardirq_trylock(cpu)	(local_irq_count == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count++)
#define hardirq_exit(cpu)	(local_irq_count--)

#define synchronize_irq()	barrier()

#define in_irq() (local_irq_count != 0)

#else

#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/smp.h>

extern unsigned int local_irq_count[NR_CPUS];
extern unsigned char global_irq_holder;
extern spinlock_t global_irq_lock;
extern atomic_t global_irq_count;

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] + local_bh_count[__cpu] != 0); })

#define in_irq() ({ int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] != 0); })

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
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
	return (! atomic_read(&global_irq_count) &&
		! spin_is_locked (&global_irq_lock));
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* __SMP__ */

#endif /* __SPARC_HARDIRQ_H */
