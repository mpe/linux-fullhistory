/*
 * linux/include/asm-arm/arch-nexuspci/system.h
 *
 * Copyright (c) 1996, 97, 98, 99, 2000 FutureTV Labs Ltd.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

extern __inline__ void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter)
		cpu_do_idle(IDLE_WAIT_SLOW);
}

#define arch_reset(mode)	do { } while (0)
#define arch_power_off()	do { } while (0)

#endif
