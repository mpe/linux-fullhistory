/*
 * linux/include/asm-arm/arch-rpc/system.h
 *
 * Copyright (c) 1996 Russell King
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/proc-fns.h>

#define arch_reset(mode) {						\
	extern void ecard_reset (int card);				\
	outb (0, IOMD_ROMCR0);						\
	ecard_reset(-1);						\
	cli();								\
	__asm__ __volatile__("msr  spsr, r1;"				\
			     "mcr  p15, 0, %0, c1, c0, 0;"		\
			     "movs pc, #0"				\
			 : 						\
			 : "r" (processor.u.armv3v4.reset()));		\
	}

#endif
