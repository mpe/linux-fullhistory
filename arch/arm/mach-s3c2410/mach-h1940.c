/* linux/arch/arm/mach-s3c2410/mach-h1940.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/h1940.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to v2.6 kernel
 *     06-Jan-2003 BJD  Updates for <arch/map.h>
 *     18-Jan-2003 BJD  Added serial port configuration
 *     17-Feb-2003 BJD  Copied to mach-ipaq.c
 *     21-Aug-2004 BJD  Added struct s3c2410_board
 *     04-Sep-2004 BJD  Changed uart init, renamed ipaq_ -> h1940_
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>

#include "s3c2410.h"
#include "devs.h"
#include "cpu.h"

static struct map_desc h1940_iodesc[] __initdata = {
	/* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg h1940_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x245,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.clock	     = &s3c2410_pclk,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};




static struct platform_device *h1940_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c2410_board h1940_board __initdata = {
	.devices       = h1940_devices,
	.devices_count = ARRAY_SIZE(h1940_devices)
};

void __init h1940_map_io(void)
{
	s3c24xx_init_io(h1940_iodesc, ARRAY_SIZE(h1940_iodesc));
	s3c2410_init_uarts(h1940_uartcfgs, ARRAY_SIZE(h1940_uartcfgs));
	s3c2410_set_board(&h1940_board);
}

void __init h1940_init_irq(void)
{
	s3c2410_init_irq();

}

void __init h1940_init_time(void)
{
	s3c2410_init_time();
}

MACHINE_START(H1940, "IPAQ-H1940")
     MAINTAINER("Ben Dooks <ben@fluff.org>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, S3C2410_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
     MAPIO(h1940_map_io)
     INITIRQ(h1940_init_irq)
     INITTIME(h1940_init_time)
MACHINE_END
