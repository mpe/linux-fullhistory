/* softirq.h: 64-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_SOFTIRQ_H
#define __SPARC64_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

/* The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t __sparc64_bh_counter;

#define get_active_bhs()	(bh_mask & bh_active)

#ifdef __SMP__
#error SMP not supported on sparc64 yet
#else

#define softirq_trylock()	(atomic_read(&__sparc64_bh_counter) ? \
				0 : \
				((atomic_set(&__sparc64_bh_counter,1)),1))
#define softirq_endlock()	(atomic_set(&__sparc64_bh_counter, 0))
#define clear_active_bhs(x)	(bh_active &= ~(x))

#define init_bh(nr, routine)	\
do {	int ent = nr;		\
	bh_base[ent] = routine;	\
	bh_mask_count[ent] = 0;	\
	bh_mask |= 1 << ent;	\
} while(0)

#define mark_bh(nr)		(bh_active |= (1 << (nr)))

#define disable_bh(nr)		\
do {	int ent = nr;		\
	bh_mask &= ~(1 << ent);	\
	bh_mask_count[ent]++;	\
} while(0)

#define enable_bh(nr)			\
do {	int ent = nr;			\
	if (!--bh_mask_count[ent])	\
		bh_mask |= 1 << ent;	\
} while(0)

#define start_bh_atomic() \
	do { atomic_inc(&__sparc64_bh_counter); synchronize_irq(); } while(0)

#define end_bh_atomic()	  do { atomic_dec(&__sparc64_bh_counter); } while(0)

#endif /* !(__SMP__) */

#endif /* !(__SPARC64_SOFTIRQ_H) */
