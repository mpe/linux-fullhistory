/*
 * Software interrupts..
 */

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

extern inline void remove_bh(int nr)
{
	bh_base[nr] = NULL;
	bh_mask &= ~(1 << nr);
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

#ifndef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */

extern int __ppc_bh_counter;

extern inline void start_bh_atomic(void)
{
	__ppc_bh_counter++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	__ppc_bh_counter--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock()	(__ppc_bh_counter? 0: ((__ppc_bh_counter=1),1))
#define softirq_endlock()	(__ppc_bh_counter = 0)

#else
#error Nein, wir haben noch kein SMP
#endif

#endif
