/*
 * linux/include/asm-arm/arch-a5k/system.h
 *
 * Copyright (c) 1996 Russell King
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_hard_reset (void)
{
	extern void ecard_reset (int card);

	/*
	 * Reset all expansion cards.
	 */
	ecard_reset (-1);

	/*
	 * copy branch instruction to reset location and call it
	 */
	*(unsigned long *)0 = *(unsigned long *)0x03800000;
	((void(*)(void))0)();

	/*
	 * If that didn't work, loop endlessly
	 */
	while (1);
}

#endif
