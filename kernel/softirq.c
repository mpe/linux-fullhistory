/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * do_bottom_half() runs at normal kernel priority: all interrupts
 * enabled.  do_bottom_half() is atomic with respect to itself: a
 * bottom_half handler need not be re-entrant.
 *
 * Fixed a disable_bh()/enable_bh() race (was causing a console lockup)
 * due bh_mask_count not atomic handling. Copyright (C) 1998  Andrea Arcangeli
 */

#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>

#include <asm/io.h>

/* intr_count died a painless death... -DaveM */

atomic_t bh_mask_count[32];
unsigned long bh_active = 0;
unsigned long bh_mask = 0;
void (*bh_base[32])(void);

/*
 * This needs to make sure that only one bottom half handler
 * is ever active at a time. We do this without locking by
 * doing an atomic increment on the intr_count, and checking
 * (nonatomically) against 1. Only if it's 1 do we schedule
 * the bottom half.
 *
 * Note that the non-atomicity of the test (as opposed to the
 * actual update) means that the test may fail, and _nobody_
 * runs the handlers if there is a race that makes multiple
 * CPU's get here at the same time. That's ok, we'll run them
 * next time around.
 */
static inline void run_bottom_halves(void)
{
	unsigned long active;
	void (**bh)(void);

	active = get_active_bhs();
	clear_active_bhs(active);
	bh = bh_base;
	do {
		if (active & 1)
			(*bh)();
		bh++;
		active >>= 1;
	} while (active);
}

asmlinkage void do_bottom_half(void)
{
	int cpu = smp_processor_id();

	if (softirq_trylock(cpu)) {
		if (hardirq_trylock(cpu)) {
			__sti();
			run_bottom_halves();
			hardirq_endlock(cpu);
		}
		softirq_endlock(cpu);
	}
}
