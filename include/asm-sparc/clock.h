/* clock.h:  Definitions for the clock/timer chips on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* Clock timer structures. The interrupt timer has two properties which
 * are the counter (which is handled in do_timer in sched.c) and the limit.
 * This limit is where the timer's counter 'wraps' around. Oddly enough,
 * the sun4c timer when it hits the limit wraps back to 1 and not zero
 * thus when calculating the value at which it will fire a microsecond you
 * must adjust by one.  Thanks SUN for designing such great hardware ;(
 */

/* Note that I am only going to use the timer that interrupts at
 * Sparc IRQ 10.  There is another one available that can fire at
 * IRQ 14. If I can think of some creative uses for it this may
 * change. It might make a nice kernel/user profiler etc.
 */

struct sparc_timer_info {
  unsigned int cur_count10;
  unsigned int timer_limit10;
  unsigned int cur_count14;
  unsigned int timer_limit14;
};

struct sparc_clock_info {
  unsigned char hsec;
  unsigned char hr;
  unsigned char min;
  unsigned char sec;
  unsigned char mon;
  unsigned char day;
  unsigned char yr;
  unsigned char wkday;
  unsigned char ram_hsec;
  unsigned char ram_hr;
  unsigned char ram_min;
  unsigned char ram_sec;
  unsigned char ram_mon;
  unsigned char ram_day;
  unsigned char ram_year;
  unsigned char ram_wkday;
  unsigned char intr_reg;
  unsigned char cmd_reg;
  unsigned char foo[14];
};

#define TIMER_PHYSADDR   0xf3000000

/* YUCK YUCK YUCK, grrr... */
#define  TIMER_STRUCT  ((struct sparc_timer_info *)((struct sparc_clock_info *) TIMER_VADDR))

