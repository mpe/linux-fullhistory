/*
 * arch/arm/kernel/leds-footbridge.c
 *
 * Copyright (C) 1998-1999 Russell King
 *
 * EBSA-285 and NetWinder LED control routines.
 *
 * The EBSA-285 uses the leds as follows:
 *  - Green - toggles state every 50 timer interrupts
 *  - Amber - On if system is not idle
 *  - Red   - currently unused
 *
 * The Netwinder uses the leds as follows:
 *  - Green - toggles state every 50 timer interrupts
 *  - Red   - On if the system is not idle
 *
 * Changelog:
 *   02-05-1999	RMK	Various cleanups
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/spinlock.h>
#include <asm/system.h>

#define LED_STATE_ENABLED	1
#define LED_STATE_CLAIMED	2
static char led_state;
static char hw_led_state;

static spinlock_t leds_lock = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_ARCH_EBSA285

static void __ebsa285_text ebsa285_leds_event(led_event_t evt)
{
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);

	switch (evt) {
	case led_start:
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN;
#ifndef CONFIG_LEDS_IDLE
		hw_led_state |= XBUS_LED_AMBER;
#endif
		led_state |= LED_STATE_ENABLED;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN | XBUS_LED_AMBER;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = XBUS_LED_RED | XBUS_LED_GREEN | XBUS_LED_AMBER;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= XBUS_LED_GREEN;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= XBUS_LED_RED;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~XBUS_LED_RED;
		break;
#endif

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_GREEN;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_GREEN;
		break;

	case led_amber_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_AMBER;
		break;

	case led_amber_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_AMBER;
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~XBUS_LED_RED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= XBUS_LED_RED;
		break;

	default:
		break;
	}

	if  (led_state & LED_STATE_ENABLED)
		*XBUS_LEDS = hw_led_state;

	spin_unlock_irqrestore(&leds_lock, flags);
}

#endif

#ifdef CONFIG_ARCH_NETWINDER

static void __netwinder_text netwinder_leds_event(led_event_t evt)
{
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);

	switch (evt) {
	case led_start:
		led_state |= LED_STATE_ENABLED;
		hw_led_state = 0;
		break;

	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = 0;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = 0;
		break;

#ifdef CONFIG_LEDS_TIMER
	case led_timer:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state ^= GPIO_GREEN_LED;
		break;
#endif

#ifdef CONFIG_LEDS_CPU
	case led_idle_start:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state &= ~GPIO_RED_LED;
		break;

	case led_idle_end:
		if (!(led_state & LED_STATE_CLAIMED))
			hw_led_state |= GPIO_RED_LED;
		break;
#endif

	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= GPIO_GREEN_LED;
		break;

	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~GPIO_GREEN_LED;
		break;

	case led_amber_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= GPIO_GREEN_LED | GPIO_RED_LED;
		break;

	case led_amber_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~(GPIO_GREEN_LED | GPIO_RED_LED);
		break;

	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= GPIO_RED_LED;
		break;

	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~GPIO_RED_LED;
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&leds_lock, flags);

	if  (led_state & LED_STATE_ENABLED) {
		spin_lock_irqsave(&gpio_lock, flags);
		gpio_modify_op(GPIO_RED_LED | GPIO_GREEN_LED, hw_led_state);
		spin_unlock_irqrestore(&gpio_lock, flags);
	}
}

#endif

static void dummy_leds_event(led_event_t evt)
{
}

__initfunc(void
init_leds_event(led_event_t evt))
{
	switch (machine_arch_type) {
#ifdef CONFIG_ARCH_EBSA285
	case MACH_TYPE_EBSA285:
		leds_event = ebsa285_leds_event;
		break;
#endif
#ifdef CONFIG_ARCH_NETWINDER
	case MACH_TYPE_NETWINDER:
		leds_event = netwinder_leds_event;
		break;
#endif

	default:
		leds_event = dummy_leds_event;
	}

	leds_event(evt);
}

void (*leds_event)(led_event_t) = init_leds_event;

EXPORT_SYMBOL(leds_event);
