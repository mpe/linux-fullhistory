/*
 * linux/include/asm-arm/arch-arc/system.h
 *
 * Copyright (c) 1996-1999 Russell King and Dave Gilbert
 */

static void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter);
}

extern __inline__ void arch_reset(char mode)
{
	extern void ecard_reset(int card);

	/*
	 * Reset all expansion cards.
	 */
	ecard_reset(-1);

	/*
	 * copy branch instruction to reset location and call it
	 */
	*(unsigned long *)0 = *(unsigned long *)0x03800000;
	((void(*)(void))0)();
}
