/*
 * linux/include/asm-arm/arch-shark/system.h
 *
 * Copyright (c) 1996-1998 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

static void arch_reset(char mode)
{
	/*
	 * loop endlessly
	 */
	cli();
}

static void arch_idle(void)
{
}

#endif
