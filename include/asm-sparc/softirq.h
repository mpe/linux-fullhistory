/* softirq.h: 32-bit Sparc soft IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC_SOFTIRQ_H
#define __SPARC_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

/* The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
#define get_active_bhs()	(bh_mask & bh_active)

#ifdef __SMP__

extern atomic_t __sparc_bh_counter;

#define start_bh_atomic() \
	do { atomic_inc(&__sparc_bh_counter); synchronize_irq(); } while(0)

#define end_bh_atomic()		atomic_dec(&__sparc_bh_counter)

#include <asm/spinlock.h>

extern spinlock_t global_bh_lock;

#define init_bh(nr, routine)				\
do {	unsigned long flags;				\
	int ent = nr;					\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_base[ent] = routine;				\
	bh_mask_count[ent] = 0;				\
	bh_mask |= 1 << ent;				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#define remove_bh(nr)					\
do {	unsigned long flags;				\
	int ent = nr;					\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_base[ent] = NULL;				\
	bh_mask &= ~(1 << ent);				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#define mark_bh(nr)					\
do {	unsigned long flags;				\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_active |= (1 << nr);				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#define disable_bh(nr)					\
do {	unsigned long flags;				\
	int ent = nr;					\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_mask &= ~(1 << ent);				\
	bh_mask_count[ent]++;				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#define enable_bh(nr)					\
do {	unsigned long flags;				\
	int ent = nr;					\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	if (!--bh_mask_count[ent])			\
		bh_mask |= 1 << ent;			\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#define softirq_trylock(cpu)					\
({								\
	int ret = 1;						\
	if(atomic_add_return(1, &__sparc_bh_counter) != 1) {	\
		atomic_dec(&__sparc_bh_counter);		\
		ret = 0;					\
	}							\
	ret;							\
})
#define softirq_endlock(cpu)	atomic_dec(&__sparc_bh_counter)
#define clear_active_bhs(mask)				\
do {	unsigned long flags;				\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_active &= ~(mask);				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#else /* !(__SMP__) */

extern int __sparc_bh_counter;

#define start_bh_atomic()	do { __sparc_bh_counter++; barrier(); } while(0)
#define end_bh_atomic()		do { barrier(); __sparc_bh_counter--; } while(0)

#define softirq_trylock(cpu) (__sparc_bh_counter ? 0 : (__sparc_bh_counter=1))
#define softirq_endlock(cpu) (__sparc_bh_counter = 0)
#define clear_active_bhs(x)	(bh_active &= ~(x))

#define init_bh(nr, routine)	\
do {	int ent = nr;		\
	bh_base[ent] = routine;	\
	bh_mask_count[ent] = 0;	\
	bh_mask |= 1 << ent;	\
} while(0)

#define remove_bh(nr)		\
do {	int ent = nr;		\
	bh_base[ent] = NULL;	\
	bh_mask &= ~(1 << ent);	\
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

#endif /* __SMP__ */

#endif /* __SPARC_SOFTIRQ_H */
