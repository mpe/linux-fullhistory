/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#ifndef __SMP__
extern unsigned int local_bh_count;
#else
#define local_bh_count		(cpu_data[smp_processor_id()].bh_count)
#endif

/* The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */

#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(mask)			\
	__asm__ __volatile__(			\
"1:	ldx	[%1], %%g7\n"			\
"	andn	%%g7, %0, %%g5\n"		\
"	casx	[%1], %%g7, %%g5\n"		\
"	cmp	%%g7, %%g5\n"			\
"	bne,pn	%%xcc, 1b\n"			\
"	 nop"					\
	: /* no outputs */			\
	: "HIr" (mask), "r" (&bh_active)	\
	: "g5", "g7", "cc", "memory")

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

#ifndef __SMP__

extern inline void start_bh_atomic(void)
{
	local_bh_count++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	local_bh_count--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(local_bh_count ? 0 : (local_bh_count=1))
#define softirq_endlock(cpu)	(local_bh_count = 0)
#define synchronize_bh()	barrier()

#else /* (__SMP__) */

extern atomic_t global_bh_lock;
extern spinlock_t global_bh_count;

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
	if (spin_trylock(&global_bh_count)) {
		if (atomic_read(&global_bh_lock) == 0) {
			++(cpu_data[cpu].bh_count);
			return 1;
		}
		spin_unlock(&global_bh_count);
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	(cpu_data[cpu].bh_count)--;
	spin_unlock(&global_bh_count);
}

#endif /* (__SMP__) */

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

#endif /* !(__SPARC64_SOFTIRQ_H) */
