/*
 * linux/include/asm-arm/arch-vnc/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 * Copyright (c) 1998 Corel Computer Corp.
 */
#include <asm/hardware.h>
#include <asm/dec21285.h>
#include <asm/leds.h>
#include <asm/io.h>

extern __inline__ void arch_reset(char mode)
{
	cli();

	/* open up the SuperIO chip
	 */
	outb(0x87, 0x370);
	outb(0x87, 0x370);

	/* aux function group 1 (Logical Device 7)
	 */
	outb(0x07, 0x370);
	outb(0x07, 0x371);

	/* set GP16 for WD-TIMER output
	 */
	outb(0xE6, 0x370);
	outb(0x00, 0x371);

	/* set a RED LED and toggle WD_TIMER for rebooting...
	 */
	outb(0xC4, 0x338);
}

#define arch_start_idle()	leds_event(led_idle_start)
#define arch_end_idle()		leds_event(led_idle_end)
