/* linux/arch/arm/mach-s3c2410/mach-rx3715.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/rx3715.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	16-Sep-2004 BJD Copied from mach-h1940.c
 *	25-Oct-2004 BJD Updates for 2.6.10-rc1
 *	10-Jan-2005 BJD Removed include of s3c2410.h s3c2440.h
 *	14-Jan-2005 BJD Added new clock init
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "pm.h"

static struct map_desc rx3715_iodesc[] __initdata = {
	/* dump ISA space somewhere unused */

	{ S3C2410_VA_ISA_WORD, S3C2410_CS3, SZ_16M, MT_DEVICE },
	{ S3C2410_VA_ISA_BYTE, S3C2410_CS3, SZ_16M, MT_DEVICE },
};

static struct s3c2410_uartcfg rx3715_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};

static struct platform_device *rx3715_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c24xx_board rx3715_board __initdata = {
	.devices       = rx3715_devices,
	.devices_count = ARRAY_SIZE(rx3715_devices)
};

void __init rx3715_map_io(void)
{
	s3c24xx_init_io(rx3715_iodesc, ARRAY_SIZE(rx3715_iodesc));
	s3c24xx_init_clocks(16934000);
	s3c24xx_init_uarts(rx3715_uartcfgs, ARRAY_SIZE(rx3715_uartcfgs));
	s3c24xx_set_board(&rx3715_board);
}

void __init rx3715_init_irq(void)
{
	s3c24xx_init_irq();
}

#ifdef CONFIG_PM
static void __init rx3715_init_machine(void)
{
	s3c2410_pm_init();
}
#else
#define rx3715_init_machine NULL
#endif

MACHINE_START(RX3715, "IPAQ-RX3715")
     MAINTAINER("Ben Dooks <ben@fluff.org>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, S3C2410_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
     MAPIO(rx3715_map_io)
     INITIRQ(rx3715_init_irq)
     INIT_MACHINE(rx3715_init_machine)
	.timer		= &s3c24xx_timer,
MACHINE_END
