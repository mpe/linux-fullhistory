#ifndef _ALPHA_SOFTIRQ_H
#define _ALPHA_SOFTIRQ_H

/* The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t __alpha_bh_counter;

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

/*
 * start_bh_atomic/end_bh_atomic also nest
 * naturally by using a counter
 */
extern inline void start_bh_atomic(void)
{
#ifdef __SMP__
	atomic_inc(&__alpha_bh_counter);
	synchronize_irq();
#else
	atomic_inc(&__alpha_bh_counter);
#endif
}

extern inline void end_bh_atomic(void)
{
	atomic_dec(&__alpha_bh_counter);
}

#ifndef __SMP__

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(atomic_read(&__alpha_bh_counter) ? \
				0 : \
				((atomic_set(&__alpha_bh_counter,1)),1))
#define softirq_endlock(cpu)	(atomic_set(&__alpha_bh_counter, 0))

#else

#error FIXME

#endif /* __SMP__ */
#endif /* _ALPHA_SOFTIRQ_H */
