#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	bh_mask_count[nr] = 0;
	bh_mask |= 1 << nr;
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
	bh_mask &= ~(1 << nr);
	bh_mask_count[nr]++;
}

extern inline void enable_bh(int nr)
{
	if (!--bh_mask_count[nr])
		bh_mask |= 1 << nr;
}

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t __intel_bh_counter;

extern inline void start_bh_atomic(void)
{
	atomic_inc(&__intel_bh_counter);
	synchronize_irq();
}

extern inline void end_bh_atomic(void)
{
	atomic_dec(&__intel_bh_counter);
}

/* These are for the irq's testing the lock */
static inline int softirq_trylock(void)
{
	atomic_inc(&__intel_bh_counter);
	if (atomic_read(&__intel_bh_counter) != 1) {
		atomic_dec(&__intel_bh_counter);
		return 0;
	}
	return 1;
}

#define softirq_endlock()	atomic_dec(&__intel_bh_counter)

#else

extern int __intel_bh_counter;

extern inline void start_bh_atomic(void)
{
	__intel_bh_counter++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	__intel_bh_counter--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock()	(__intel_bh_counter ? 0 : (__intel_bh_counter=1))
#define softirq_endlock()	(__intel_bh_counter = 0)

#endif	/* SMP */

#endif	/* __ASM_SOFTIRQ_H */
