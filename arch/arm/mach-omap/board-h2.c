/*
 * linux/arch/arm/mach-omap/board-h2.c
 *
 * Board specific inits for OMAP-1610 H2
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * H2 specific changes and cleanup
 * Copyright (C) 2004 Nokia Corporation by Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/clocks.h>
#include <asm/arch/gpio.h>
#include <asm/arch/usb.h>

#include "common.h"

extern int omap_gpio_init(void);

static int __initdata h2_serial_ports[OMAP_MAX_NR_PORTS] = {1, 1, 1};

static struct resource h2_smc91x_resources[] = {
	[0] = {
		.start	= OMAP1610_ETHR_START,		/* Physical */
		.end	= OMAP1610_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(0),
		.end	= OMAP_GPIO_IRQ(0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device h2_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(h2_smc91x_resources),
	.resource	= h2_smc91x_resources,
};

static struct platform_device *h2_devices[] __initdata = {
	&h2_smc91x_device,
};

static void __init h2_init_smc91x(void)
{
	if ((omap_request_gpio(0)) < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}
	omap_set_gpio_edge_ctrl(0, OMAP_GPIO_FALLING_EDGE);
}

void h2_init_irq(void)
{
	omap_init_irq();
	omap_gpio_init();
	h2_init_smc91x();
}

static struct omap_usb_config h2_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg		= 2,

#ifdef	CONFIG_USB_GADGET_OMAP
	.hmc_mode	= 19,	// 0:host(off) 1:dev|otg 2:disabled
	// .hmc_mode	= 21,	// 0:host(off) 1:dev(loopback) 2:host(loopback)
#elif	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* needs OTG cable, or NONSTANDARD (B-to-MiniB) */
	.hmc_mode	= 20,	// 1:dev|otg(off) 1:host 2:disabled
#endif

	.pins[1]	= 3,
};

static struct omap_mmc_config h2_mmc_config __initdata = {
	.mmc_blocks		= 1,
	.mmc1_power_pin		= -1,	/* tps65010 gpio3 */
	.mmc1_switch_pin	= OMAP_MPUIO(1),
};

static struct omap_board_config_kernel h2_config[] = {
	{ OMAP_TAG_USB,           &h2_usb_config },
	{ OMAP_TAG_MMC,           &h2_mmc_config },
};

static void __init h2_init(void)
{
	platform_add_devices(h2_devices, ARRAY_SIZE(h2_devices));
	omap_board_config = h2_config;
	omap_board_config_size = ARRAY_SIZE(h2_config);
}

static void __init h2_map_io(void)
{
	omap_map_io();
	omap_serial_init(h2_serial_ports);
}

MACHINE_START(OMAP_H2, "TI-H2")
	MAINTAINER("Imre Deak <imre.deak@nokia.com>")
	BOOT_MEM(0x10000000, 0xfff00000, 0xfef00000)
	BOOT_PARAMS(0x10000100)
	MAPIO(h2_map_io)
	INITIRQ(h2_init_irq)
	INIT_MACHINE(h2_init)
	.timer		= &omap_timer,
MACHINE_END
