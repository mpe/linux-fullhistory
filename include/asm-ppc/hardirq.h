#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <asm/smp.h>

extern unsigned int local_irq_count[NR_CPUS];

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] + local_bh_count[__cpu] != 0); })

#define in_irq() (local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#define synchronize_irq()	do { } while (0)

#else /* __SMP__ */

#include <asm/atomic.h>

extern unsigned char global_irq_holder;
extern unsigned volatile int global_irq_lock;
extern atomic_t global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		clear_bit(0,&global_irq_lock);
	}
}

static inline void hardirq_enter(int cpu)
{
	unsigned int loops = 10000000;
	
	++local_irq_count[cpu];
	atomic_inc(&global_irq_count);
	while (test_bit(0,&global_irq_lock)) {
		if (smp_processor_id() == global_irq_holder) {
			printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
			xmon(0);
#endif
			break;
		}
		if (loops-- == 0) {
			printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
			xmon(0);
#endif
		}
	}
}

static inline void hardirq_exit(int cpu)
{
	atomic_dec(&global_irq_count);
	--local_irq_count[cpu];
}

static inline int hardirq_trylock(int cpu)
{
	return !atomic_read(&global_irq_count) && !test_bit(0,&global_irq_lock);
}

#define hardirq_endlock(cpu)	do { } while (0)

extern void synchronize_irq(void);

#endif /* __SMP__ */

#endif /* __ASM_HARDIRQ_H */
