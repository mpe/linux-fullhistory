/*
 * linux/include/asm-arm/arch-ebsa110/system.h
 *
 * Copyright (c) 1996-1999 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#define arch_do_idle()		cpu_do_idle()
#define arch_power_off()	do { } while (0)
#define arch_reset(mode)	cpu_reset(0x80000000)

#endif
