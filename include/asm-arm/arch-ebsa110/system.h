/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

/*
 * This machine must never stop it MCLK.  However, if we are
 * idle for a long time, slow the processor clock to MCLK.
 */
extern __inline__ void arch_idle(void)
{
	unsigned long start_idle;

	start_idle = jiffies;

	do {
		if (current->need_resched || hlt_counter)
			goto slow_out;
	} while (time_before(start_idle, jiffies + HZ/3));

	cpu_do_idle(IDLE_CLOCK_SLOW);

	while (!current->need_resched && !hlt_counter) {
		/* do nothing slowly */
	}

	cpu_do_idle(IDLE_CLOCK_FAST);
slow_out:
}

#define arch_power_off()	do { } while (0)
#define arch_reset(mode)	cpu_reset(0x80000000)

#endif
