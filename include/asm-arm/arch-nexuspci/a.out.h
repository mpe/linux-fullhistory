/*
 * linux/include/asm-arm/arch-nexuspci/a.out.h
 *
 * Copyright (C) 1996 Russell King
 */

#ifndef __ASM_ARCH_A_OUT_H
#define __ASM_ARCH_A_OUT_H

#ifdef __KERNEL__
#define STACK_TOP		((current->personality==PER_LINUX_32BIT)? 0xc0000000 : 0x04000000)
#endif

#endif

