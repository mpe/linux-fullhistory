/*
 * linux/include/asm-arm/arch-sa1100/system.h
 *
 * Copyright (c) 1999 Nicolas Pitre <nico@cam.org>
 */
#include <linux/config.h>

extern __inline__ void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter) {
		cpu_do_idle(IDLE_CLOCK_SLOW);
		cpu_do_idle(IDLE_WAIT_FAST);
		cpu_do_idle(IDLE_CLOCK_FAST);
	}
}

#ifdef CONFIG_SA1100_VICTOR

extern inline void arch_power_off(void)
{
	/* switch off power supply */
	mdelay(2000);
	GPCR = GPIO_GPIO23;
	while(1);
}

/* power off unconditionally */
#define arch_reset(x) arch_power_off()

#else

extern inline void arch_reset(char mode)
{
	if (mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		/* Activate SA1100 watchdog and wait for the trigger... */
		OSMR3 = OSCR + 3686400/2;	/* in 1/2 sec */
		OWER |= OWER_WME;
		OIER |= OIER_E3;
	}
}

#define arch_power_off()	do { } while (0)

#endif
