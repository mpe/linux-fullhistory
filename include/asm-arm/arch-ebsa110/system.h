/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_hard_reset (void)
{
	/*
	 * loop endlessly
	 */
	cli();
	while (1);
}

#endif
