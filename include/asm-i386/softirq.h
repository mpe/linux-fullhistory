#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

/*
 * Software interrupts..
 */
#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

#ifdef __SMP__

/* These are for the irq's testing the lock */
static inline int softirq_trylock(void)
{
	atomic_inc(&intr_count);
	if (intr_count != 1) {
		atomic_dec(&intr_count);
		return 0;
	}
	return 1;
}

#define softirq_endlock()	atomic_dec(&intr_count)

#else

/* These are for the irq's testing the lock */
#define softirq_trylock()	(intr_count ? 0 : ((intr_count=1),1))
#define softirq_endlock()	(intr_count = 0)


#endif

#endif
