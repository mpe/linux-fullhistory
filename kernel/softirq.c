/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * do_bottom_half() runs at normal kernel priority: all interrupts
 * enabled.  do_bottom_half() is atomic with respect to itself: a
 * bottom_half handler need not be re-entrant.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>

#define INCLUDE_INLINE_FUNCS
#include <linux/tqueue.h>

unsigned long intr_count = 0;

unsigned long bh_active = 0;
unsigned long bh_mask = 0;
struct bh_struct bh_base[32];


asmlinkage void do_bottom_half(void)
{
	unsigned long active;
	unsigned long mask, left;
	struct bh_struct *bh;

	bh = bh_base;
	active = bh_active & bh_mask;
	for (mask = 1, left = ~0 ; left & active ; bh++,mask += mask,left += left) {
		if (mask & active) {
			void (*fn)(void *);
			bh_active &= ~mask;
			fn = bh->routine;
			if (!fn)
				goto bad_bh;
			fn(bh->data);
		}
	}
	return;
bad_bh:
	printk ("irq.c:bad bottom half entry %08lx\n", mask);
}
