/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/threads.h>
#include <linux/brlock.h>
#include <linux/spinlock.h>

#ifndef __SMP__
extern unsigned int local_irq_count;
#define irq_enter(cpu, irq)	(local_irq_count++)
#define irq_exit(cpu, irq)	(local_irq_count--)
#else
#define local_irq_count		(__brlock_array[smp_processor_id()][BR_GLOBALIRQ_LOCK])
#define irq_enter(cpu, irq)	br_read_lock(BR_GLOBALIRQ_LOCK)
#define irq_exit(cpu, irq)	br_read_unlock(BR_GLOBALIRQ_LOCK)
#endif

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?  On any cpu?
 */
#define in_interrupt() ((local_irq_count + local_bh_count) != 0)

/* This tests only the local processors hw IRQ context disposition.  */
#define in_irq() (local_irq_count != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	((void)(cpu), local_irq_count == 0)
#define hardirq_endlock(cpu)	do { (void)(cpu); } while(0)

#define hardirq_enter(cpu)	((void)(cpu), local_irq_count++)
#define hardirq_exit(cpu)	((void)(cpu), local_irq_count--)

#define synchronize_irq()	barrier()

#else /* (__SMP__) */

#include <linux/spinlock.h>

static __inline__ int irqs_running(void)
{
	enum brlock_indices idx = BR_GLOBALIRQ_LOCK;
	int i, count = 0;

	for (i = 0; i < smp_num_cpus; i++)
		count += (__brlock_array[cpu_logical_map(i)][idx] != 0);

	return count;
}

extern unsigned char global_irq_holder;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore... */
	if(global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		br_write_unlock(BR_GLOBALIRQ_LOCK);
	}
}

static inline int hardirq_trylock(int cpu)
{
	spinlock_t *lock = &__br_write_locks[BR_GLOBALIRQ_LOCK].lock;

	return (!irqs_running() && !spin_is_locked(lock));
}

#define hardirq_endlock(cpu)	do { (void)(cpu); } while (0)

extern void synchronize_irq(void);

#endif /* __SMP__ */

#endif /* !(__SPARC64_HARDIRQ_H) */
