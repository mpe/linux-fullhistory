/*
 * linux/include/asm-arm/arch-cl7500/time.h
 *
 * Copyright (c) 1996-2000 Russell King.
 *
 * Changelog:
 *  24-Sep-1996	RMK	Created
 *  10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *  04-Dec-1997	RMK	Updated for new arch/arm/time.c
 */
extern void ioctime_init(void);

static void timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);
	do_set_rtc();

	{
		/* Twinkle the lights. */
		static int count, bit = 8, dir = 1;
		if (count-- == 0) {
			bit += dir;
			if (bit == 8 || bit == 15) dir = -dir;
			count = 5;
			*((volatile unsigned int *)(0xe002ba00)) = 1 << bit;
		}
	}

	do_profile(regs);
}

/*
 * Set up timer interrupt.
 */
extern __inline__ void setup_timer(void)
{
	ioctime_init();

	timer_irq.handler = timer_interrupt;

	setup_arm_irq(IRQ_TIMER, &timer_irq);
}
