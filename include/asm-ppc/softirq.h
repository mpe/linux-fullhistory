/*
 * Software interrupts..
 */

#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
#define get_active_bhs()	(bh_mask & bh_active)

#ifndef __SMP__

extern int __ppc_bh_counter;

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

#else /* __SMP__ */

extern atomic_t __ppc_bh_counter;

#define start_bh_atomic() \
	do { atomic_inc(&__ppc_bh_counter); synchronize_irq(); } while(0)

#define end_bh_atomic()		atomic_dec(&__ppc_bh_counter)

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

#define softirq_trylock()					\
({								\
	int ret = 1;						\
	if(atomic_add_return(1, &__ppc_bh_counter) != 1) {	\
		atomic_dec(&__ppc_bh_counter);		\
		ret = 0;					\
	}							\
	ret;							\
})
#define softirq_endlock()	atomic_dec(&__ppc_bh_counter)
#define clear_active_bhs(mask)				\
do {	unsigned long flags;				\
	spin_lock_irqsave(&global_bh_lock, flags);	\
	bh_active &= ~(mask);				\
	spin_unlock_irqrestore(&global_bh_lock, flags);	\
} while(0)

#endif /* __SMP__ */

#endif
