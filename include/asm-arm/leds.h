/*
 * include/asm-arm/leds.h
 *
 * Copyright (C) 1998 Russell King
 *
 * Event-driven interface for LEDs on machines
 */
#ifndef ASM_ARM_LEDS_H
#define ASM_ARM_LEDS_H

typedef enum {
	led_idle_start,
	led_idle_end,
	led_timer
} led_event_t;

/* Use this routine to handle LEDs */
extern void leds_event(led_event_t);

#endif
