/*
 * $Id: time.h,v 1.3 1997/08/12 08:22:14 cort Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

/* time.c */
__inline__ unsigned long get_dec(void);
__inline__ void set_dec(int val);
void prep_calibrate_decr_handler(int, void *,struct pt_regs *);
void prep_calibrate_decr(void);
void pmac_calibrate_decr(void);
extern unsigned decrementer_count;
extern unsigned count_period_num;
extern unsigned count_period_den;

/* pmac/prep_time.c */
unsigned long prep_get_rtc_time(void);
unsigned long pmac_get_rtc_time(void);
int prep_set_rtc_time(unsigned long nowtime);
int pmac_set_rtc_time(unsigned long nowtime);
void pmac_read_rtc_time(void);

