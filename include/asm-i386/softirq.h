#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

extern unsigned int local_bh_count[NR_CPUS];

#define cpu_bh_disable(cpu)	do { local_bh_count[(cpu)]++; barrier(); } while (0)
#define cpu_bh_enable(cpu)	do { barrier(); local_bh_count[(cpu)]--; } while (0)

#define cpu_bh_trylock(cpu)	(local_bh_count[(cpu)] ? 0 : (local_bh_count[(cpu)] = 1))
#define cpu_bh_endlock(cpu)	(local_bh_count[(cpu)] = 0)

#define local_bh_disable()	cpu_bh_disable(smp_processor_id())
#define local_bh_enable()	cpu_bh_enable(smp_processor_id())

#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;
extern spinlock_t i386_bh_lock;

extern void synchronize_bh(void);

static inline void start_bh_atomic(void)
{
	atomic_inc(&global_bh_lock);
	synchronize_bh();
}

static inline void end_bh_atomic(void)
{
	atomic_dec(&global_bh_lock);
}

/* These are for the IRQs testing the lock */
static inline int softirq_trylock(int cpu)
{
	if (cpu_bh_trylock(cpu)) {
		if (!test_and_set_bit(0,&global_bh_count)) {
			if (atomic_read(&global_bh_lock) == 0)
				return 1;
			clear_bit(0,&global_bh_count);
		}
		cpu_bh_endlock(cpu);
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	cpu_bh_enable(cpu);
	clear_bit(0,&global_bh_count);
}

#else

extern inline void start_bh_atomic(void)
{
	local_bh_disable();
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	local_bh_enable();
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(cpu_bh_trylock(cpu))
#define softirq_endlock(cpu)	(cpu_bh_endlock(cpu))
#define synchronize_bh()	barrier()

#endif	/* SMP */

extern inline void init_bh(int nr, void (*routine)(void))
{
	unsigned long flags;

	bh_base[nr] = routine;
	atomic_set(&bh_mask_count[nr], 0);

	spin_lock_irqsave(&i386_bh_lock, flags);
	bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&i386_bh_lock, flags);
}

extern inline void remove_bh(int nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i386_bh_lock, flags);
	bh_mask &= ~(1 << nr);
	spin_unlock_irqrestore(&i386_bh_lock, flags);

	synchronize_bh();
	bh_base[nr] = NULL;
}

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i386_bh_lock, flags);
	bh_mask &= ~(1 << nr);
	atomic_inc(&bh_mask_count[nr]);
	spin_unlock_irqrestore(&i386_bh_lock, flags);
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	unsigned long flags;

	spin_lock_irqsave(&i386_bh_lock, flags);
	if (atomic_dec_and_test(&bh_mask_count[nr]))
		bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&i386_bh_lock, flags);
}

#endif	/* __ASM_SOFTIRQ_H */
