#ifndef _ASM_M32R_TIMEX_H
#define _ASM_M32R_TIMEX_H

/* $Id$ */

/*
 * linux/include/asm-m32r/timex.h
 *
 * m32r architecture timex specifications
 */

#include <linux/config.h>

#define CLOCK_TICK_RATE	(CONFIG_BUS_CLOCK / CONFIG_TIMER_DIVIDE)
#define CLOCK_TICK_FACTOR	20	/* Factor of both 1000000 and CLOCK_TICK_RATE */
#define FINETUNE ((((((long)LATCH * HZ - CLOCK_TICK_RATE) << SHIFT_HZ) * \
	(1000000/CLOCK_TICK_FACTOR) / (CLOCK_TICK_RATE/CLOCK_TICK_FACTOR)) \
		<< (SHIFT_SCALE-SHIFT_HZ)) / HZ)

#ifdef __KERNEL__
/*
 * Standard way to access the cycle counter.
 * Currently only used on SMP.
 */

typedef unsigned long long cycles_t;

extern cycles_t cacheflush_time;

static __inline__ cycles_t get_cycles (void)
{
	return 0;
}
#endif  /* __KERNEL__ */

#endif  /* _ASM_M32R_TIMEX_H */
