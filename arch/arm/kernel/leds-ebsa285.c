/*
 * arch/arm/kernel/leds-ebsa285.c
 *
 * Copyright (C) 1998 Russell King
 *
 * EBSA-285 LED control routines.  We use the leds as follows:
 *
 *  - Green - toggles state every 50 timer interrupts
 *  - Amber - On if system is not idle
 *  - Red   - currently unused
 */
#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

static char led_state = XBUS_LED_RED | XBUS_LED_GREEN;

void leds_event(led_event_t ledevt)
{
	unsigned long flags;

	save_flags_cli(flags);

	switch(ledevt) {
	case led_idle_start:
		led_state |= XBUS_LED_AMBER;
		break;

	case led_idle_end:
		led_state &= ~XBUS_LED_AMBER;
		break;

	case led_timer:
		led_state ^= XBUS_LED_GREEN;
		break;

	default:
		break;
	}

	restore_flags(flags);

	*XBUS_LEDS = led_state;
}
