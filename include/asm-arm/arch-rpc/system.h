/*
 * linux/include/asm-arm/arch-rpc/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#include <asm/arch/hardware.h>
#include <asm/iomd.h>
#include <asm/io.h>

static void arch_idle(void)
{
	unsigned long start_idle;

	start_idle = jiffies;

	do {
		if (current->need_resched || hlt_counter)
			goto slow_out;
		cpu_do_idle(IDLE_WAIT_FAST);
	} while (time_before(start_idle, jiffies + HZ/3));

	cpu_do_idle(IDLE_CLOCK_SLOW);

	while (!current->need_resched && !hlt_counter) {
		cpu_do_idle(IDLE_WAIT_SLOW);
	}

	cpu_do_idle(IDLE_CLOCK_FAST);
slow_out:
}

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	ecard_reset(-1);

	outb(0, IOMD_ROMCR0);

	/*
	 * Jump into the ROM
	 */
	cpu_reset(0);
}
