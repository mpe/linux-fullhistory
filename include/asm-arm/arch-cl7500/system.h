/*
 * linux/include/asm-arm/arch-cl7500/system.h
 *
 * Copyright (c) 1999 Nexus Electronics Ltd.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/iomd.h>

#define arch_do_idle()				\
	outb(0, IOMD_SUSMODE)

#define arch_reset(mode)			\
	do {					\
		outb (0, IOMD_ROMCR0);		\
		cpu_reset(0);			\
	} while (0);

#define arch_power_off()	do { } while (0)

#endif
