/*
 * linux/include/asm-arm/arch-ebsa285/timex.h
 *
 * EBSA285 architecture timex specifications
 *
 * Copyright (C) 1998 Russell King
 */

/*
 * On the EBSA, the clock ticks at weird rates.
 * This is therefore not used to calculate the
 * divisor.
 */
#define CLOCK_TICK_RATE		(50000000 / 16)
