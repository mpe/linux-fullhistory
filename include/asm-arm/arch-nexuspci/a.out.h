/*
 * linux/include/asm-arm/arch-nexuspci/a.out.h
 *
 * Copyright (C) 1996-1999 Russell King
 */
#ifndef __ASM_ARCH_A_OUT_H
#define __ASM_ARCH_A_OUT_H

#include <asm/arch/memory.h>

#define STACK_TOP \
	((current->personality == PER_LINUX_32BIT) ? \
	 TASK_SIZE : 0x04000000)

#endif

