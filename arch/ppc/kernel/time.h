/*
 * $Id: time.h,v 1.12 1999/08/27 04:21:23 cort Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#include <linux/config.h>
#include <linux/mc146818rtc.h>

#include <asm/processor.h>

/* time.c */
extern unsigned decrementer_count;
extern unsigned count_period_num;
extern unsigned count_period_den;
extern unsigned long mktime(unsigned int, unsigned int, unsigned int,
			    unsigned int, unsigned int, unsigned int);
extern void to_tm(int tim, struct rtc_time * tm);
extern time_t last_rtc_update;

int via_calibrate_decr(void);

/* Accessor functions for the decrementer register. */
static __inline__ unsigned int get_dec(void)
{
#if defined(CONFIG_4xx)
	return (mfspr(SPRN_PIT));
#else
	return (mfspr(SPRN_DEC));
#endif
}

static __inline__ void set_dec(unsigned int val)
{
#if defined(CONFIG_4xx)
	mtspr(SPRN_PIT, val);
#else
	mtspr(SPRN_DEC, val);
#endif
}
