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

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	atomic_set(&bh_mask_count[nr], 0);
	bh_mask |= 1 << nr;
}

extern inline void remove_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	mb();
	bh_base[nr] = NULL;
}

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

#ifdef __SMP__
#error SMP not supported
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

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	atomic_inc(&bh_mask_count[nr]);
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	if (atomic_dec_and_test(&bh_mask_count[nr]))
		bh_mask |= 1 << nr;
}

#endif	/* __ASM_SOFTIRQ_H */
