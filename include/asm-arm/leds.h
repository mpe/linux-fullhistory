/*
 * include/asm-arm/leds.h
 *
 * Copyright (C) 1998 Russell King
 *
 * Event-driven interface for LEDs on machines
 *
 * Added led_start and led_stop- Alex Holden, 28th Dec 1998.
 */
#ifndef ASM_ARM_LEDS_H
#define ASM_ARM_LEDS_H

#include <linux/config.h>

typedef enum {
	led_idle_start,
	led_idle_end,
	led_timer,
	led_start,
	led_stop,
	led_claim,		/* override idle & timer leds */
	led_release,		/* restore idle & timer leds */
	led_green_on,
	led_green_off,
	led_amber_on,
	led_amber_off,
	led_red_on,
	led_red_off
} led_event_t;

/* Use this routine to handle LEDs */

#ifdef CONFIG_LEDS
extern void (*leds_event)(led_event_t);
#define set_leds_event(r)	leds_event = r
#else
#define leds_event(e)
#define set_leds_event(r)
#endif

#endif
