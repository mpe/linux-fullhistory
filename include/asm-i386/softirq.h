#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

extern unsigned int local_bh_count[NR_CPUS];
#define in_bh()	(local_bh_count[smp_processor_id()] != 0)

#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	bh_mask_count[nr] = 0;
	bh_mask |= 1 << nr;
}

extern inline void remove_bh(int nr)
{
	bh_base[nr] = NULL;
	bh_mask &= ~(1 << nr);
}

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;

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

/* These are for the irq's testing the lock */
static inline int softirq_trylock(int cpu)
{
	unsigned long flags;

	__save_flags(flags);
	__cli();
	atomic_inc(&global_bh_count);
	if (atomic_read(&global_bh_count) != 1 || atomic_read(&global_bh_lock) != 0) {
		atomic_dec(&global_bh_count);
		__restore_flags(flags);
		return 0;
	}
	++local_bh_count[cpu];
	return 1;
}

static inline void softirq_endlock(int cpu)
{
	__cli();
	atomic_dec(&global_bh_count);
	local_bh_count[cpu]--;
	__sti();
}

#else

extern inline void start_bh_atomic(void)
{
	local_bh_count[smp_processor_id()]++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	local_bh_count[smp_processor_id()]--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(in_bh() ? 0 : (local_bh_count[smp_processor_id()]=1))
#define softirq_endlock(cpu)	(local_bh_count[smp_processor_id()] = 0)
#define synchronize_bh()	do { } while (0)

#endif	/* SMP */

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	bh_mask_count[nr]++;
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	if (!--bh_mask_count[nr])
		bh_mask |= 1 << nr;
}

#endif	/* __ASM_SOFTIRQ_H */
