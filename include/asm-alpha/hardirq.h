#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

/* Initially just a straight copy of the i386 code.  */

#include <linux/threads.h>

#ifndef __SMP__
extern int __local_irq_count;
#define local_irq_count(cpu)  ((void)(cpu), __local_irq_count)
#else
#define local_irq_count(cpu)  (cpu_data[cpu].irq_count)
#endif

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */

#define in_interrupt()						\
({								\
	int __cpu = smp_processor_id();				\
	(local_irq_count(__cpu) + local_bh_count(__cpu)) != 0;	\
})

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count(cpu) == 0)
#define hardirq_endlock(cpu)	((void) 0)

#define hardirq_enter(cpu, irq)	(local_irq_count(cpu)++)
#define hardirq_exit(cpu, irq)	(local_irq_count(cpu)--)

#define synchronize_irq()	barrier()

#else

#include <asm/atomic.h>
#include <asm/spinlock.h>
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
	++local_irq_count(cpu);
        atomic_inc(&global_irq_count);
}

static inline void hardirq_exit(int cpu, int irq)
{
	atomic_dec(&global_irq_count);
        --local_irq_count(cpu);
}

static inline int hardirq_trylock(int cpu)
{
	return (!atomic_read(&global_irq_count)
		&& !spin_is_locked(&global_irq_lock));
}

#define hardirq_endlock(cpu)  ((void)0)

extern void synchronize_irq(void);

#endif /* __SMP__ */
#endif /* _ALPHA_HARDIRQ_H */
