/* $Id: softirq.h,v 1.6 1999/06/17 13:30:38 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999 by Ralf Baechle
 */
#ifndef __ASM_MIPS_SOFTIRQ_H
#define __ASM_MIPS_SOFTIRQ_H

/* The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t __mips_bh_counter;

extern unsigned int local_bh_count[NR_CPUS];

#define cpu_bh_disable(cpu)    do { local_bh_count[(cpu)]++; barrier(); } while (0)
#define cpu_bh_enable(cpu)     do { barrier(); local_bh_count[(cpu)]--; } while (0)

#define cpu_bh_trylock(cpu)    (local_bh_count[(cpu)] ? 0 : (local_bh_count[(cpu)] = 1))
#define cpu_bh_endlock(cpu)    (local_bh_count[(cpu)] = 0)

#define local_bh_disable()     cpu_bh_disable(smp_processor_id())
#define local_bh_enable()      cpu_bh_enable(smp_processor_id())

#define get_active_bhs()	(bh_mask & bh_active)

static inline void clear_active_bhs(unsigned long x)
{
	unsigned long temp;

	__asm__ __volatile__(
		"1:\tll\t%0,%1\n\t"
		"and\t%0,%2\n\t"
		"sc\t%0,%1\n\t"
		"beqz\t%0,1b"
		:"=&r" (temp),
		 "=m" (bh_active)
		:"Ir" (~x),
		 "m" (bh_active));
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
	mb();
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
	bh_mask &= ~(1 << nr);
	atomic_inc(&bh_mask_count[nr]);
}

extern inline void enable_bh(int nr)
{
	if (atomic_dec_and_test(&bh_mask_count[nr]))
		bh_mask |= 1 << nr;
}

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
#define softirq_trylock(cpu)   (cpu_bh_trylock(cpu))
#define softirq_endlock(cpu)   (cpu_bh_endlock(cpu))
#define synchronize_bh()	barrier()

#endif /* __ASM_MIPS_SOFTIRQ_H */
