/*
 * linux/include/asm-arm/arch-sa1100/time.h
 *
 * Copyright (C) 1998 Deborah Wallach.
 * Twiddles  (C) 1999 	Hugo Fiennes <hugo@empeg.com>
 * 
 * 2000/03/29 (C) Nicolas Pitre <nico@cam.org>
 *	Rewritten: big cleanup, much simpler, better HZ acuracy.
 *
 */

#include <asm/arch/hardware.h>
#include <asm/arch/irqs.h>


/* IRQs are disabled before entering here from do_gettimeofday() */
static unsigned long sa1100_gettimeoffset (void)
{
	unsigned long ticks_to_match, elapsed, usec;

	/* Get ticks before next timer match */
	ticks_to_match = OSMR0 - OSCR;

	/* We need elapsed ticks since last match */
	elapsed = LATCH - ticks_to_match;

	/* Now convert them to usec */
	usec = (unsigned long)(elapsed*tick)/LATCH;

	return usec;
}


static void sa1100_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int next_match;

	/* Loop until we get ahead of the free running timer.
	 * This ensures an exact clock tick count and time acuracy.
	 * Should be IRQ race free.
	 */
	do {
		do_timer(regs);
		OSSR = OSSR_M0;  /* Clear match on timer 0 */
		next_match = (OSMR0 += LATCH);
	} while( (signed long)(next_match - OSCR) <= 0 );
}


extern inline void setup_timer (void)
{
	gettimeoffset = sa1100_gettimeoffset;
	timer_irq.handler = sa1100_timer_interrupt;
	OSMR0 = 0;		/* set initial match at 0 */
	OSSR = 0xf;		/* clear status on all timers */
	setup_arm_irq(IRQ_OST0, &timer_irq);
	OIER |= OIER_E0;	/* enable match on timer 0 to cause interrupts */
	OSCR = 0;		/* initialize free-running timer, force first match */
}

