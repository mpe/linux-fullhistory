/*
 * linux/include/asm-arm/arch-l7200/system.h
 *
 * Copyright (c) 2000 Steven Hill (sjhill@cotw.com)
 *
 * Changelog
 *  03-21-2000  SJH	Created
 *  04-26-2000  SJH	Fixed functions
 *  05-03-2000  SJH	Removed usage of obsolete 'iomd.h'
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_idle(void)
{
        while (!current->need_resched && !hlt_counter)
		{ };
/*                outb(0, IOMD_SUSMODE);*/
}

#define arch_power_off()	do { } while (0)

extern inline void arch_reset(char mode)
{
	if (mode == 's') {
		cpu_reset(0);
	}
}

#endif
