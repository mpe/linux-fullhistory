/* $Id: timer.h,v 1.12 1996/03/24 20:21:29 davem Exp $
 * timer.h:  Definitions for the timer chips on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_TIMER_H
#define _SPARC_TIMER_H

#include <asm/system.h>  /* For NCPUS */

/* Timer structures. The interrupt timer has two properties which
 * are the counter (which is handled in do_timer in sched.c) and the limit.
 * This limit is where the timer's counter 'wraps' around. Oddly enough,
 * the sun4c timer when it hits the limit wraps back to 1 and not zero
 * thus when calculating the value at which it will fire a microsecond you
 * must adjust by one.  Thanks SUN for designing such great hardware ;(
 */

/* Note that I am only going to use the timer that interrupts at
 * Sparc IRQ 10.  There is another one available that can fire at
 * IRQ 14. Currently it is left untouched, we keep the PROM's limit
 * register value and let the prom take these interrupts.  This allows
 * L1-A to work.
 */

struct sun4c_timer_info {
  volatile unsigned int cur_count10;
  volatile unsigned int timer_limit10;
  volatile unsigned int cur_count14;
  volatile unsigned int timer_limit14;
};

#define SUN4C_TIMER_PHYSADDR   0xf3000000

/* A sun4m has two blocks of registers which are probably of the same
 * structure. LSI Logic's L64851 is told to _decrement_ from the limit
 * value. Aurora behaves similarly but its limit value is compacted in
 * other fashion (it's wider). Documented fields are defined here.
 */

/* As with the interrupt register, we have two classes of timer registers
 * which are per-cpu and master.  Per-cpu timers only hit that cpu and are
 * only level 14 ticks, master timer hits all cpus and is level 10.
 */

#define SUN4M_PRM_CNT_L       0x80000000
#define SUN4M_PRM_CNT_LVALUE  0x7FFFFC00

struct sun4m_timer_percpu_info {
  volatile unsigned int l14_timer_limit;    /* Initial value is 0x009c4000 */
  volatile unsigned int l14_cur_count;

  /* This register appears to be write only and/or inaccessible
   * on Uni-Processor sun4m machines.
   */
  volatile unsigned int l14_limit_noclear;  /* Data access error is here */

  volatile unsigned int cntrl;            /* =1 after POST on Aurora */
  volatile unsigned char space[PAGE_SIZE - 16];
};

struct sun4m_timer_regs {
	struct sun4m_timer_percpu_info cpu_timers[NCPUS];
	volatile unsigned int l10_timer_limit;
	volatile unsigned int l10_cur_count;

	/* Again, this appears to be write only and/or inaccessible
	 * on uni-processor sun4m machines.
	 */
	volatile unsigned int l10_limit_noclear;

	/* This register too, it must be magic. */
	volatile unsigned int foobar;

	volatile unsigned int cfg;     /* equals zero at boot time... */
};

extern struct sun4m_timer_regs *sun4m_timers;
extern volatile unsigned int *master_l10_counter;
extern volatile unsigned int *master_l10_limit;

#endif /* !(_SPARC_TIMER_H) */
