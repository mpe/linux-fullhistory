/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#define arch_do_idle()		cpu_do_idle()

extern __inline__ void arch_reset(char mode)
{
	if (mode == 's') {
		__asm__ volatile(
		"mcr	p15, 0, %0, c1, c0, 0	@ MMU off
		 mov	pc, #0x80000000		@ jump to flash"
		: : "r" (cpu_reset()) : "cc");
	}
}

#endif
