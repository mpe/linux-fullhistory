/*
 * linux/arch/arm/mach-omap/omap-perseus2.c
 *
 * Modified from omap-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/clocks.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>

#include <asm/arch/omap-perseus2.h>

#include "common.h"

void omap_perseus2_init_irq(void)
{
	omap_init_irq();
}

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP730_FPGA_ETHR_START,	/* Physical */
		.end	= OMAP730_FPGA_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= INT_ETHER,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct platform_device *devices[] __initdata = {
	&smc91x_device,
};

static void __init omap_perseus2_init(void)
{
	if (!machine_is_omap_perseus2())
		return;

	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
}

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc omap_perseus2_io_desc[] __initdata = {
	{OMAP730_FPGA_BASE, OMAP730_FPGA_START, OMAP730_FPGA_SIZE,
	 MT_DEVICE},
};

static void __init omap_perseus2_map_io(void)
{
	omap_map_io();
	iotable_init(omap_perseus2_io_desc,
		     ARRAY_SIZE(omap_perseus2_io_desc));

	/* Early, board-dependent init */

	/*
	 * Hold GSM Reset until needed
	 */
	*DSP_M_CTL &= ~1;

	/*
	 * UARTs -> done automagically by 8250 driver
	 */

	/*
	 * CSx timings, GPIO Mux ... setup
	 */

	/* Flash: CS0 timings setup */
	*((volatile __u32 *) OMAP_FLASH_CFG_0) = 0x0000fff3;
	*((volatile __u32 *) OMAP_FLASH_ACFG_0) = 0x00000088;

	/*
	 * Ethernet support trough the debug board
	 * CS1 timings setup
	 */
	*((volatile __u32 *) OMAP_FLASH_CFG_1) = 0x0000fff3;
	*((volatile __u32 *) OMAP_FLASH_ACFG_1) = 0x00000000;

	/*
	 * Configure MPU_EXT_NIRQ IO in IO_CONF9 register,
	 * It is used as the Ethernet controller interrupt
	 */
	*((volatile __u32 *) PERSEUS2_IO_CONF_9) &= 0x1FFFFFFF;
}

MACHINE_START(OMAP_PERSEUS2, "OMAP730 Perseus2")
	MAINTAINER("Kevin Hilman <k-hilman@ti.com>")
	BOOT_MEM(0x10000000, 0xe0000000, 0xe0000000)
	BOOT_PARAMS(0x10000100)
	MAPIO(omap_perseus2_map_io)
	INITIRQ(omap_perseus2_init_irq)
	INIT_MACHINE(omap_perseus2_init)
MACHINE_END
