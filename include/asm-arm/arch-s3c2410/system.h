/* linux/include/asm-arm/arch-s3c2410/system.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - System function defines and includes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  12-May-2003 BJD  Created file
 *  14-May-2003 BJD  Removed idle to aid debugging
 *  12-Jun-2003 BJD  Added reset via watchdog
 *  04-Sep-2003 BJD  Moved to v2.6
 *  28-Oct-2004 BJD  Added over-ride for idle, and fixed reset panic()
 */

#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/arch/map.h>
#include <asm/arch/idle.h>

#include <asm/arch/regs-watchdog.h>
#include <asm/arch/regs-clock.h>

void (*s3c24xx_idle)(void);

void s3c24xx_default_idle(void)
{
	void __iomem *reg = S3C2410_CLKCON;
	unsigned long tmp;
	int i;

	/* idle the system by using the idle mode which will wait for an
	 * interrupt to happen before restarting the system.
	 */

	/* Warning: going into idle state upsets jtag scanning */

	__raw_writel(__raw_readl(reg) | (1<<2), reg);

	/* the samsung port seems to do a loop and then unset idle.. */
	for (i = 0; i < 50; i++) {
		tmp += __raw_readl(reg); /* ensure loop not optimised out */
	}

	/* this bit is not cleared on re-start... */

	__raw_writel(__raw_readl(reg) & ~(1<<2), reg);
}

static void arch_idle(void)
{
	if (s3c24xx_idle != NULL)
		(s3c24xx_idle)();
	else
		s3c24xx_default_idle();
}


static void
arch_reset(char mode)
{
	if (mode == 's') {
		cpu_reset(0);
	}

	printk("arch_reset: attempting watchdog reset\n");

	__raw_writel(0, S3C2410_WTCON);	  /* disable watchdog, to be safe  */

	/* put initial values into count and data */
	__raw_writel(0x100, S3C2410_WTCNT);
	__raw_writel(0x100, S3C2410_WTDAT);

	/* set the watchdog to go and reset... */
	__raw_writel(S3C2410_WTCON_ENABLE|S3C2410_WTCON_DIV16|S3C2410_WTCON_RSTEN |
		     S3C2410_WTCON_PRESCALE(0x80), S3C2410_WTCON);

	/* wait for reset to assert... */
	mdelay(5000);

	printk(KERN_ERR "Watchdog reset failed to assert reset\n");

	/* we'll take a jump through zero as a poor second */
	cpu_reset(0);
}
