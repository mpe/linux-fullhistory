#ifndef _ALPHA_SOFTIRQ_H
#define _ALPHA_SOFTIRQ_H

#include <linux/stddef.h>
#include <asm/atomic.h>
#include <asm/hardirq.h>

extern unsigned int local_bh_count[NR_CPUS];

extern inline void cpu_bh_disable(int cpu)
{
	local_bh_count[cpu]++;
	mb();
}

extern inline void cpu_bh_enable(int cpu)
{
	mb();
	local_bh_count[cpu]--;
}

extern inline int cpu_bh_trylock(int cpu)
{
	return local_bh_count[cpu] ? 0 : (local_bh_count[cpu] = 1);
}

extern inline void cpu_bh_endlock(int cpu)
{
	local_bh_count[cpu] = 0;
}

#define local_bh_enable()	cpu_bh_enable(smp_processor_id())
#define local_bh_disable()	cpu_bh_disable(smp_processor_id())

#define get_active_bhs()	(bh_mask & bh_active)

static inline void clear_active_bhs(unsigned long x)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	bic %0,%2,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".section .text2,\"ax\"\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (bh_active)
	:"Ir" (x), "m" (bh_active));
}

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	atomic_set(&bh_mask_count[nr], 0);
	bh_mask |= 1 << nr;
}

extern inline void remove_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	wmb();
	bh_base[nr] = NULL;
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
	if (cpu_bh_trylock(cpu)) {
		if (!test_and_set_bit(0, &global_bh_count)) {
			if (atomic_read(&global_bh_lock) == 0)
				return 1;
			clear_bit(0, &global_bh_count);
		}
		cpu_bh_endlock(cpu);
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	cpu_bh_enable(cpu);
	clear_bit(0, &global_bh_count);
}

#else

extern inline void start_bh_atomic(void)
{
	local_bh_disable();
}

extern inline void end_bh_atomic(void)
{
	local_bh_enable();
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	cpu_bh_trylock(cpu)
#define softirq_endlock(cpu)	cpu_bh_endlock(cpu)
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

#endif /* _ALPHA_SOFTIRQ_H */
