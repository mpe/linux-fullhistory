#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

extern unsigned int local_irq_count[NR_CPUS];
#define in_interrupt()	(local_irq_count[smp_processor_id()] != 0)

#ifndef __SMP__

#define hardirq_trylock(cpu)	((cpu)==0)	/* always true */
#define hardirq_endlock(cpu)	do { } while (0)

#define hardirq_enter(cpu)	(local_irq_count[cpu]++)
#define hardirq_exit(cpu)	(local_irq_count[cpu]--)

#else

extern unsigned char global_irq_holder;
extern unsigned volatile int global_irq_lock;
extern unsigned volatile int global_irq_count;

static inline void release_irqlock(int cpu)
{
	/* if we didn't own the irq lock, just ignore.. */
	if (global_irq_holder == (unsigned char) cpu) {
		global_irq_holder = NO_PROC_ID;
		global_irq_lock = 0;
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

	__save_flags(flags);
	__cli();
	atomic_inc(&global_irq_count);
	if (global_irq_count != 1 || test_bit(0,&global_irq_lock)) {
		atomic_dec(&global_irq_count);
		__restore_flags(flags);
		return 0;
	}
	++local_irq_count[cpu];
	__sti();
	return 1;
}

static inline void hardirq_endlock(int cpu)
{
	__cli();
	hardirq_exit(cpu);
	__sti();
}	

#endif /* __SMP__ */

#endif /* __ASM_HARDIRQ_H */
