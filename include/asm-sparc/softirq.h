/* softirq.h: 32-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#ifndef __SPARC_SOFTIRQ_H
#define __SPARC_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/hardirq.h>

extern unsigned int local_bh_count[NR_CPUS];

#define get_active_bhs()	(bh_mask & bh_active)

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;
extern spinlock_t sparc_bh_lock;

extern void synchronize_bh(void);

static inline void clear_active_bhs(unsigned int mask)
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	bh_active &= ~(mask);
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
}

extern inline void init_bh(int nr, void (*routine)(void))
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	bh_base[nr] = routine;
	bh_mask_count[nr] = 0;
	bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
}

extern inline void remove_bh(int nr)
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	bh_base[nr] = NULL;
	bh_mask &= ~(1 << nr);
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
}

extern inline void mark_bh(int nr)
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	bh_active |= (1 << nr);
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
}

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	bh_mask &= ~(1 << nr);
	bh_mask_count[nr]++;
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	unsigned long flags;
	spin_lock_irqsave(&sparc_bh_lock, flags);
	if (!--bh_mask_count[nr])
		bh_mask |= 1 << nr;
	spin_unlock_irqrestore(&sparc_bh_lock, flags);
}

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
	if (atomic_add_return(1, &global_bh_count) == 1) {
		if (atomic_read(&global_bh_lock) == 0) {
			++local_bh_count[cpu];
			return 1;
		}
	}
	atomic_dec(&global_bh_count);
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	local_bh_count[cpu]--;
	atomic_dec(&global_bh_count);
}

#else

#define clear_active_bhs(x)	(bh_active &= ~(x))
#define mark_bh(nr)		(bh_active |= (1 << (nr)))

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(local_bh_count[cpu] ? 0 : (local_bh_count[cpu]=1))
#define softirq_endlock(cpu)	(local_bh_count[cpu] = 0)
#define synchronize_bh()	barrier()

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

extern inline void start_bh_atomic(void)
{
	local_bh_count[0]++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	local_bh_count[0]--;
}

#endif	/* SMP */

#endif	/* __SPARC_SOFTIRQ_H */
