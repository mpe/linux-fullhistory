/* linux/arch/arm/mach-s3c2410/mach-otom.c
 *
 * Copyright (c) 2004 Nex Vision
 *   Guillaume GOURAT <guillaume.gourat@nexvision.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/otom-map.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>

#include "s3c2410.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"

static struct map_desc otom11_iodesc[] __initdata = {
  /* Device area */
	{ (u32)OTOM_VA_CS8900A_BASE, OTOM_PA_CS8900A_BASE, SZ_16M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG12 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg otom11_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	}
};

/* NOR Flash on NexVision OTOM board */

static struct resource otom_nor_resource[] = {
	[0] = {
		.start = S3C2410_CS0,
		.end   = S3C2410_CS0 + (4*1024*1024) - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device otom_device_nor = {
	.name		= "mtd-flash",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(otom_nor_resource),
	.resource	= otom_nor_resource,
};

/* Standard OTOM devices */

static struct platform_device *otom11_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
 	&s3c_device_rtc,
	&otom_device_nor,
};

static struct s3c24xx_board otom11_board __initdata = {
	.devices       = otom11_devices,
	.devices_count = ARRAY_SIZE(otom11_devices)
};


void __init otom11_map_io(void)
{
	s3c24xx_init_io(otom11_iodesc, ARRAY_SIZE(otom11_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(otom11_uartcfgs, ARRAY_SIZE(otom11_uartcfgs));
	s3c24xx_set_board(&otom11_board);
}


MACHINE_START(OTOM, "Nex Vision - Otom 1.1")
     MAINTAINER("Guillaume GOURAT <guillaume.gourat@nexvision.tv>")
     BOOT_MEM(S3C2410_SDRAM_PA, S3C2410_PA_UART, (u32)S3C24XX_VA_UART)
     BOOT_PARAMS(S3C2410_SDRAM_PA + 0x100)
	.map_io		= otom11_map_io,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END
