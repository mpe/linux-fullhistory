/*
 * linux/include/asm-i386/timex.h
 *
 * i386 architecture timex specifications
 */
#ifndef _ASMi386_TIMEX_H
#define _ASMi386_TIMEX_H

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

/*
 * Standard way to access the cycle counter on i586+ CPUs.
 * Currently only used on SMP.
 */
typedef unsigned long long cycles_t;

extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	cycles_t value;

	__asm__("rdtsc"
		:"=a" (*(((int *)&value)+0)),
		 "=d" (*(((int *)&value)+1)));
	return value;
}

#endif
