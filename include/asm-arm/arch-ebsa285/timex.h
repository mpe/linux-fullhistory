/*
 * linux/include/asm-arm/arch-ebsa285/timex.h
 *
 * EBSA285 architecture timex specifications
 *
 * Copyright (C) 1998 Russell King
 */

/*
 * On EBSA285 boards, the clock runs at 50MHz and is
 * divided by a 4-bit prescaler.  Other boards use an
 * ISA derived timer, and this is unused.
 */
#define CLOCK_TICK_RATE		(mem_fclk_21285 / 16)
