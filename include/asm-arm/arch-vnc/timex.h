/*
 * linux/include/asm-arm/arch-vnc/timex.h
 *
 * Corel Video NC architecture timex specifications
 *
 * Copyright (C) 1998 Corel Computer/Russell King
 */

/*
 * On the VNC, the clock runs at 66MHz and is divided
 * by a 4-bit prescaler.
 */
#define CLOCK_TICK_RATE		(66000000 / 16)
