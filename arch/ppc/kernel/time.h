/*
 * $Id: time.h,v 1.10 1998/04/01 07:46:03 geert Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#include <linux/mc146818rtc.h>

/* time.c */
void prep_calibrate_decr_handler(int, void *,struct pt_regs *);
void prep_calibrate_decr(void);
void pmac_calibrate_decr(void);
extern void apus_calibrate_decr(void);
extern unsigned decrementer_count;
extern unsigned count_period_num;
extern unsigned count_period_den;
extern unsigned long mktime(unsigned int, unsigned int,unsigned int,
			    unsigned int, unsigned int, unsigned int);
extern void to_tm(int tim, struct rtc_time * tm);
extern unsigned long last_rtc_update;

/* pmac/prep/chrp_time.c */
unsigned long prep_get_rtc_time(void);
unsigned long pmac_get_rtc_time(void);
unsigned long chrp_get_rtc_time(void);
unsigned long apus_get_rtc_time(void);
int prep_set_rtc_time(unsigned long nowtime);
int pmac_set_rtc_time(unsigned long nowtime);
int chrp_set_rtc_time(unsigned long nowtime);
int apus_set_rtc_time(unsigned long nowtime);
void pmac_read_rtc_time(void);
void chrp_calibrate_decr(void);
void chrp_time_init(void);
int via_calibrate_decr(void);
void mbx_calibrate_decr(void);

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
