/*
 * arch/arm/kernel/leds-ebsa110.c
 *
 * Copyright (C) 1998 Russell King
 *
 * EBSA-110 LED control routines.  We use the led as follows:
 *
 *  - Red - toggles state every 50 timer interrupts
 */
#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

void leds_event(led_event_t ledevt)
{
	unsigned long flags;

	save_flags_cli(flags);

	switch(ledevt) {
	case led_timer:
		*(volatile unsigned char *)0xf2400000 ^= 128;
		break;

	default:
		break;
	}

	restore_flags(flags);
}
