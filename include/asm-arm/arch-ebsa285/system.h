/*
 * linux/include/asm-arm/arch-ebsa285/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 */
#include <asm/hardware.h>
#include <asm/leds.h>

extern __inline__ void arch_reset(char mode)
{
	cli();

	if (mode == 's') {
		__asm__ volatile (
		"mov	lr, #0x41000000		@ prepare to jump to ROM
		 mov	r0, #0x130
		 mcr	p15, 0, r0, c1, c0	@ MMU off
		 mcr	p15, 0, ip, c7, c7	@ flush caches
		 mov	pc, lr");
	} else {
		/* To reboot, we set up the 21285 watchdog and enable it.
		 * We then wait for it to timeout.
		 */
		*CSR_TIMER4_LOAD = 0x8000;
		*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;
		*CSR_SA110_CNTL |= 1 << 13;
	}
}

#define arch_start_idle()	leds_event(led_idle_start)
#define arch_end_idle()		leds_event(led_idle_end)
