/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (c) 1996-1998 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_reset(char mode)
{
	/*
	 * loop endlessly
	 */
	cli();
}

#define arch_power_off()   do { } while (0)
#define arch_do_idle() do {} while (0)
/*cpu_do_idle()*/

#endif
