/*
 * linux/include/asm-arm/arch-ebsa285/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 */
#include <asm/hardware.h>
#include <asm/leds.h>

/* To reboot, we set up the 21285 watchdog and enable it.
 * We then wait for it to timeout.
 */
extern __inline__ void arch_hard_reset (void)
{
	cli();
	*CSR_TIMER4_LOAD = 0x8000;
	*CSR_TIMER4_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;
	*CSR_SA110_CNTL |= 1 << 13;
	while(1);
}

#define ARCH_IDLE_OK

#define arch_start_idle()	leds_event(led_idle_start)
#define arch_end_idle()		leds_event(led_idle_end)
