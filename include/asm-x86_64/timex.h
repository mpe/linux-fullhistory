/*
 * linux/include/asm-x86_64/timex.h
 *
 * x86-64 architecture timex specifications
 */
#ifndef _ASMx8664_TIMEX_H
#define _ASMx8664_TIMEX_H

#include <linux/config.h>
#include <asm/8253pit.h>
#include <asm/msr.h>
#include <asm/vsyscall.h>
#include <asm/hpet.h>

#define CLOCK_TICK_RATE	PIT_TICK_RATE	/* Underlying HZ */

typedef unsigned long long cycles_t;

extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	unsigned long long ret;

	rdtscll(ret);
	return ret;
}

extern unsigned int cpu_khz;

extern struct vxtime_data vxtime;

#endif
