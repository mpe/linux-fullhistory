/*
 * $Id: time.h,v 1.11 1999/03/18 04:16:34 cort Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#include <linux/mc146818rtc.h>

/* time.c */
extern unsigned decrementer_count;
extern unsigned count_period_num;
extern unsigned count_period_den;
extern unsigned long mktime(unsigned int, unsigned int, unsigned int,
			    unsigned int, unsigned int, unsigned int);
extern void to_tm(int tim, struct rtc_time * tm);
extern unsigned long last_rtc_update;

int via_calibrate_decr(void);

/* Accessor functions for the decrementer register. */
static __inline__ unsigned int get_dec(void)
{
	unsigned int ret;

	asm volatile("mfspr %0,22" : "=r" (ret) :);
	return ret;
}

static __inline__ void set_dec(unsigned int val)
{
	asm volatile("mtspr 22,%0" : : "r" (val));
}
