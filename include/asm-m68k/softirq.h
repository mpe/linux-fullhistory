#ifndef __M68K_SOFTIRQ_H
#define __M68K_SOFTIRQ_H

/*
 * Software interrupts.. no SMP here either.
 */
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

extern int __m68k_bh_counter;

extern inline void start_bh_atomic(void)
{
	__m68k_bh_counter++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	__m68k_bh_counter--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock()  (__m68k_bh_counter ? 0 : (__m68k_bh_counter=1))
#define softirq_endlock()  (__m68k_bh_counter = 0)

#endif
#ifndef __M68K_SOFTIRQ_H
#define __M68K_SOFTIRQ_H

/*
 * Software interrupts.. no SMP here either.
 */
#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	bh_mask_count[nr] = 0;
	bh_mask |= 1 << nr;
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

extern int __m68k_bh_counter;

extern inline void start_bh_atomic(void)
{
	__m68k_bh_counter++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	__m68k_bh_counter--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock()  (__m68k_bh_counter ? 0 : (__m68k_bh_counter=1))
#define softirq_endlock()  (__m68k_bh_counter = 0)

#endif
