/*
 * linux/include/asm-arm/arch-nexuspci/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_hard_reset (void)
{
	/*
	 * loop endlessly - the watchdog will reset us if it's enabled.
	 */
	cli();
	while (1);
}

#endif
