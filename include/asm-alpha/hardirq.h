#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

#include <linux/tasks.h>

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt() (local_irq_count[smp_processor_id()] + local_bh_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while (0)

#else

/* initially just a straight copy if the i386 code */

#include <asm/atomic.h>
#include <asm/spinlock.h>
#include <asm/system.h>
#include <asm/smp.h>

extern unsigned char global_irq_holder;
extern spinlock_t global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		spin_unlock(&global_irq_lock);
        }
}

/* Ordering of the counter bumps is _deadly_ important. */
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
	int ret = 1;

        __save_and_cli(flags);
        if ((atomic_add_return(1, &global_irq_count) != 1) ||
	    (global_irq_lock.lock != 0)) {
		atomic_dec(&global_irq_count);
                __restore_flags(flags);
                ret = 0;
        } else {
		++local_irq_count[cpu];
                __sti();
        }
        return ret;
}

#define hardirq_endlock(cpu) \
	do { \
	       __cli(); \
		hardirq_exit(cpu); \
		__sti(); \
	} while (0)

extern void synchronize_irq(void);

#endif /* __SMP__ */
#endif /* _ALPHA_HARDIRQ_H */
