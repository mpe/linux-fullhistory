/*
 * $Id: time.h,v 1.5 1997/08/27 22:06:58 cort Exp $
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#include <linux/mc146818rtc.h>

/* time.c */
__inline__ unsigned long get_dec(void);
__inline__ void set_dec(int val);
void prep_calibrate_decr_handler(int, void *,struct pt_regs *);
void prep_calibrate_decr(void);
void pmac_calibrate_decr(void);
void chrp_calibrate_decr(void);
extern unsigned decrementer_count;
extern unsigned count_period_num;
extern unsigned count_period_den;
extern unsigned long mktime(unsigned int, unsigned int,unsigned int,
				   unsigned int, unsigned int, unsigned int);

/* pmac/prep/chrp_time.c */
unsigned long prep_get_rtc_time(void);
unsigned long pmac_get_rtc_time(void);
unsigned long chrp_get_rtc_time(void);
int prep_set_rtc_time(unsigned long nowtime);
int pmac_set_rtc_time(unsigned long nowtime);
int chrp_set_rtc_time(unsigned long nowtime);
void pmac_read_rtc_time(void);

#define TICK_SIZE tick
#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int      month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

extern void inline to_tm(int tim, struct rtc_time * tm)
{
	register int    i;
	register long   hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;
}
