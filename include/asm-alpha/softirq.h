#ifndef _ALPHA_SOFTIRQ_H
#define _ALPHA_SOFTIRQ_H

/*
 * Software interrupts..
 */
#define get_active_bhs()	(bh_mask & bh_active)

static inline void clear_active_bhs(unsigned long x)
{
	unsigned long temp;
	__asm__ __volatile__(
	"1:	ldq_l %0,%1\n"
	"	and %0,%2,%0\n"
	"	stq_c %0,%1\n"
	"	beq %0,2f\n"
	".text 2\n"
	"2:	br 1b\n"
	".text"
	:"=&r" (temp), "=m" (bh_active)
	:"Ir" (x), "m" (bh_active));
}

#ifndef __SMP__

/* These are for the irq's testing the lock */
#define softirq_trylock()	(intr_count ? 0 : ((intr_count=1),1))
#define softirq_endlock()	(intr_count = 0)

#else

#error FIXME

#endif /* __SMP__ */
#endif /* _ALPHA_SOFTIRQ_H */
