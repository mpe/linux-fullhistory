/*
 * arch/arm/mach-omap/usb.c -- platform level USB initialization
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef	DEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/usb_otg.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/board.h>

/* These routines should handle the standard chip-specific modes
 * for usb0/1/2 ports, covering basic mux and transceiver setup.
 * Call omap_usb_init() once, from INIT_MACHINE().
 *
 * Some board-*.c files will need to set up additional mux options,
 * like for suspend handling, vbus sensing, GPIOs, and the D+ pullup.
 */

/* TESTED ON:
 *  - 1611B H2 (with usb1 mini-AB) using standard Mini-B or OTG cables
 *  - 1510 Innovator UDC with bundled usb0 cable
 *  - 1510 Innovator OHCI with bundled usb1/usb2 cable
 *  - 1510 Innovator OHCI with custom usb0 cable, feeding 5V VBUS
 *  - 1710 custom development board using alternate pin group
 */

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_ARCH_OMAP_OTG

static struct otg_transceiver *xceiv;

/**
 * otg_get_transceiver - find the (single) OTG transceiver driver
 *
 * Returns the transceiver driver, after getting a refcount to it; or
 * null if there is no such transceiver.  The caller is responsible for
 * releasing that count.
 */
struct otg_transceiver *otg_get_transceiver(void)
{
	if (xceiv)
		get_device(xceiv->dev);
	return xceiv;
}
EXPORT_SYMBOL(otg_get_transceiver);

int otg_set_transceiver(struct otg_transceiver *x)
{
	if (xceiv && x)
		return -EBUSY;
	xceiv = x;
	return 0;
}
EXPORT_SYMBOL(otg_set_transceiver);

#endif

/*-------------------------------------------------------------------------*/

static u32 __init omap_usb0_init(unsigned nwires, unsigned is_device)
{
	u32	syscon1 = 0;

	if (nwires == 0) {
		if (!cpu_is_omap15xx()) {
			/* pulldown D+/D- */
			USB_TRANSCEIVER_CTRL_REG &= ~(3 << 1);
		}
		return 0;
	}

	/*
	 * VP and VM are needed for all active usb0 configurations.
	 * USB0_VP and USB0_VM are always set on 1510, there's no muxing
	 * available for them.
	 */
	if (nwires >= 2 && !cpu_is_omap15xx()) {
		omap_cfg_reg(AA9_USB0_VP);
		omap_cfg_reg(R9_USB0_VM);
	}
	if (is_device)
		omap_cfg_reg(W4_USB_PUEN);

	/* internal transceiver */
	if (nwires == 2) {
		if (cpu_is_omap15xx()) {
			/* This works for OHCI on 1510-Innovator */
			return 0;
		}

		/* NOTE:  host OR device mode for now, no OTG */
		USB_TRANSCEIVER_CTRL_REG &= ~(7 << 4);
		if (is_device) {
			omap_cfg_reg(R18_1510_USB_GPIO0);
			// omap_cfg_reg(USB0_VBUS);
			// USB_TRANSCEIVER_CTRL_REG.CONF_USB0_PORT_R = 7
		} else /* host mode needs D+ and D- pulldowns */
			USB_TRANSCEIVER_CTRL_REG &= ~(3 << 1);

		return 3 << 16;
	}

	/* alternate pin config, external transceiver */
	omap_cfg_reg(V6_USB0_TXD);
	omap_cfg_reg(W9_USB0_TXEN);
	omap_cfg_reg(W5_USB0_SE0);

#ifdef CONFIG_ARCH_OMAP_USB_SPEED
	/* FIXME: there's good chance that pin V9 is used for MMC2 port cmddir */
	omap_cfg_reg(V9_USB0_SPEED);
	// omap_cfg_reg(V9_USB0_SUSP);
#endif

	if (nwires != 3)
		omap_cfg_reg(Y5_USB0_RCV);

	switch (nwires) {
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 6:
		syscon1 = 3;
		/* REVISIT: Is CONF_USB2_UNI_R only needed when nwires = 6? */
		USB_TRANSCEIVER_CTRL_REG |= CONF_USB2_UNI_R;
		break;
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			0, nwires);
	}
	return syscon1 << 16;
}

static u32 __init omap_usb1_init(unsigned nwires)
{
	u32	syscon1 = 0;

	if (nwires != 6 && !cpu_is_omap15xx())
		USB_TRANSCEIVER_CTRL_REG &= ~CONF_USB1_UNI_R;
	if (nwires == 0)
		return 0;

	/* external transceiver */
	omap_cfg_reg(USB1_TXD);
	omap_cfg_reg(USB1_TXEN);
	if (cpu_is_omap15xx()) {
		omap_cfg_reg(USB1_SEO);
		omap_cfg_reg(USB1_SPEED);
		// SUSP
	} else if (cpu_is_omap16xx()) {
		omap_cfg_reg(W13_1610_USB1_SE0);
		omap_cfg_reg(R13_1610_USB1_SPEED);
		// SUSP
	} else {
		pr_debug("usb unrecognized\n");
	}
	if (nwires != 3)
		omap_cfg_reg(USB1_RCV);

	switch (nwires) {
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 6:
		syscon1 = 3;
		omap_cfg_reg(USB1_VP);
		omap_cfg_reg(USB1_VM);
		if (!cpu_is_omap15xx())
			USB_TRANSCEIVER_CTRL_REG |= CONF_USB1_UNI_R;
		break;
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			1, nwires);
	}
	return syscon1 << 20;
}

static u32 __init omap_usb2_init(unsigned nwires, unsigned alt_pingroup)
{
	u32	syscon1 = 0;

	if (alt_pingroup || nwires == 0)
		return 0;
	if (nwires != 6 && !cpu_is_omap15xx())
		USB_TRANSCEIVER_CTRL_REG &= ~CONF_USB2_UNI_R;
	if (nwires == 0)
		return 0;

	/* external transceiver */
	if (cpu_is_omap15xx()) {
		omap_cfg_reg(USB2_TXD);
		omap_cfg_reg(USB2_TXEN);
		omap_cfg_reg(USB2_SEO);
		if (nwires != 3)
			omap_cfg_reg(USB2_RCV);
		/* there is no USB2_SPEED */
	} else if (cpu_is_omap16xx()) {
		omap_cfg_reg(V6_USB2_TXD);
		omap_cfg_reg(W9_USB2_TXEN);
		omap_cfg_reg(W5_USB2_SE0);
		if (nwires != 3)
			omap_cfg_reg(Y5_USB2_RCV);
		// FIXME omap_cfg_reg(USB2_SPEED);
	} else {
		pr_debug("usb unrecognized\n");
	}
	// omap_cfg_reg(USB2_SUSP);

	switch (nwires) {
	case 3:
		syscon1 = 2;
		break;
	case 4:
		syscon1 = 1;
		break;
	case 6:
		syscon1 = 3;
		if (cpu_is_omap15xx()) {
			omap_cfg_reg(USB2_VP);
			omap_cfg_reg(USB2_VM);
		} else {
			omap_cfg_reg(AA9_USB2_VP);
			omap_cfg_reg(R9_USB2_VM);
			USB_TRANSCEIVER_CTRL_REG |= CONF_USB2_UNI_R;
		}
		break;
	default:
		printk(KERN_ERR "illegal usb%d %d-wire transceiver\n",
			2, nwires);
	}
	return syscon1 << 24;
}

/*-------------------------------------------------------------------------*/

#if	defined(CONFIG_USB_GADGET_OMAP) || \
	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE) || \
	(defined(CONFIG_USB_OTG) && defined(CONFIG_ARCH_OMAP_OTG))
static void usb_release(struct device *dev)
{
	/* normally not freed */
}
#endif

#ifdef	CONFIG_USB_GADGET_OMAP

static struct resource udc_resources[] = {
	/* order is significant! */
	{		/* registers */
		.start		= IO_ADDRESS(UDC_BASE),
		.end		= IO_ADDRESS(UDC_BASE + 0xff),
		.flags		= IORESOURCE_MEM,
	}, {		/* general IRQ */
		.start		= IH2_BASE + 20,
		.flags		= IORESOURCE_IRQ,
	}, {		/* PIO IRQ */
		.start		= IH2_BASE + 30,
		.flags		= IORESOURCE_IRQ,
	}, {		/* SOF IRQ */
		.start		= IH2_BASE + 29,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 udc_dmamask = ~(u32)0;

static struct platform_device udc_device = {
	.name		= "omap_udc",
	.id		= -1,
	.dev = {
		.release		= usb_release,
		.dma_mask		= &udc_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(udc_resources),
	.resource	= udc_resources,
};

#endif

#if	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = ~(u32)0;

static struct resource ohci_resources[] = {
	{
		.start	= IO_ADDRESS(OMAP_OHCI_BASE),
		.end	= IO_ADDRESS(OMAP_OHCI_BASE + 4096),
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HHC_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ohci_device = {
	.name			= "ohci",
	.id			= -1,
	.dev = {
		.release		= usb_release,
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ohci_resources),
	.resource		= ohci_resources,
};

#endif

#if	defined(CONFIG_USB_OTG) && defined(CONFIG_ARCH_OMAP_OTG)

static struct resource otg_resources[] = {
	/* order is significant! */
	{
		.start		= IO_ADDRESS(OTG_BASE),
		.end		= IO_ADDRESS(OTG_BASE + 0xff),
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IH2_BASE + 8,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device otg_device = {
	.name		= "omap_otg",
	.id		= -1,
	.dev = {
		.release		= usb_release,
	},
	.num_resources	= ARRAY_SIZE(otg_resources),
	.resource	= otg_resources,
};

#endif

/*-------------------------------------------------------------------------*/

// FIXME correct answer depends on hmc_mode,
// as does any nonzero value for config->otg port number
#ifdef	CONFIG_USB_GADGET_OMAP
#define	is_usb0_device(config)	1
#else
#define	is_usb0_device(config)	0
#endif

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_ARCH_OMAP_OTG

void __init
omap_otg_init(struct omap_usb_config *config)
{
	u32		syscon = OTG_SYSCON_1_REG & 0xffff;
	int		status;
	int		alt_pingroup = 0;

	/* NOTE:  no bus or clock setup (yet?) */

	syscon = OTG_SYSCON_1_REG & 0xffff;
	if (!(syscon & OTG_RESET_DONE))
		pr_debug("USB resets not complete?\n");

	// OTG_IRQ_EN_REG = 0;

	/* pin muxing and transceiver pinouts */
	if (config->pins[0] > 2)	/* alt pingroup 2 */
		alt_pingroup = 1;
	syscon |= omap_usb0_init(config->pins[0], is_usb0_device(config));
	syscon |= omap_usb1_init(config->pins[1]);
	syscon |= omap_usb2_init(config->pins[2], alt_pingroup);
	pr_debug("OTG_SYSCON_1_REG = %08x\n", syscon);
	OTG_SYSCON_1_REG = syscon;

	syscon = config->hmc_mode;
	syscon |= USBX_SYNCHRO | (4 << 16) /* B_ASE0_BRST */;
	if (config->otg || config->register_host)
		syscon |= UHOST_EN;
#ifdef	CONFIG_USB_OTG
	if (config->otg)
		syscon |= OTG_EN;
#endif
	pr_debug("OTG_SYSCON_2_REG = %08x\n", syscon);
	OTG_SYSCON_2_REG = syscon;

	printk("USB: hmc %d", config->hmc_mode);
	if (alt_pingroup)
		printk(", usb2 alt %d wires", config->pins[2]);
	else if (config->pins[0])
		printk(", usb0 %d wires%s", config->pins[0],
			is_usb0_device(config) ? " (dev)" : "");
	if (config->pins[1])
		printk(", usb1 %d wires", config->pins[1]);
	if (!alt_pingroup && config->pins[2])
		printk(", usb2 %d wires", config->pins[2]);
	if (config->otg)
		printk(", Mini-AB on usb%d", config->otg - 1);
	printk("\n");

	/* don't clock unused USB controllers  */
	syscon = OTG_SYSCON_1_REG;
	syscon |= HST_IDLE_EN|DEV_IDLE_EN|OTG_IDLE_EN;

#ifdef	CONFIG_USB_GADGET_OMAP
	if (config->otg || config->register_dev) {
		syscon &= ~DEV_IDLE_EN;
		udc_device.dev.platform_data = config;
		status = platform_device_register(&udc_device);
		if (status)
			pr_debug("can't register UDC device, %d\n", status);
	}
#endif

#if	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	if (config->otg || config->register_host) {
		syscon &= ~HST_IDLE_EN;
		ohci_device.dev.platform_data = config;
		status = platform_device_register(&ohci_device);
		if (status)
			pr_debug("can't register OHCI device, %d\n", status);
	}
#endif

#ifdef	CONFIG_USB_OTG
	if (config->otg) {
		syscon &= ~OTG_IDLE_EN;
		if (cpu_is_omap730())
			otg_resources[1].start = INT_730_USB_OTG;
		status = platform_device_register(&otg_device);
		// ...
	}
#endif
	pr_debug("OTG_SYSCON_1_REG = %08x\n", syscon);
	OTG_SYSCON_1_REG = syscon;

	status = 0;
}

#else
static inline void omap_otg_init(struct omap_usb_config *config) {}
#endif

/*-------------------------------------------------------------------------*/

#ifdef	CONFIG_ARCH_OMAP1510

#define ULPD_SOFT_REQ_REG	__REG16(ULPD_SOFT_REQ)
#define SOFT_UDC_REQ		(1 << 4)
#define SOFT_DPLL_REQ		(1 << 0)

#define ULPD_DPLL_CTRL_REG	__REG16(ULPD_DPLL_CTRL)
#define DPLL_IOB		(1 << 13)
#define DPLL_PLL_ENABLE		(1 << 4)
#define DPLL_LOCK		(1 << 0)

#define ULPD_APLL_CTRL_REG	__REG16(ULPD_APLL_CTRL)
#define APLL_NDPLL_SWITCH	(1 << 0)


static void __init omap_1510_usb_init(struct omap_usb_config *config)
{
	int status;
	unsigned int val;

	omap_usb0_init(config->pins[0], is_usb0_device(config));
	omap_usb1_init(config->pins[1]);
	omap_usb2_init(config->pins[2], 0);

	val = omap_readl(MOD_CONF_CTRL_0) & ~(0x3f << 1);
	val |= (config->hmc_mode << 1);
	omap_writel(val, MOD_CONF_CTRL_0);

	printk("USB: hmc %d", config->hmc_mode);
	if (config->pins[0])
		printk(", usb0 %d wires%s", config->pins[0],
			is_usb0_device(config) ? " (dev)" : "");
	if (config->pins[1])
		printk(", usb1 %d wires", config->pins[1]);
	if (config->pins[2])
		printk(", usb2 %d wires", config->pins[2]);
	printk("\n");

	/* use DPLL for 48 MHz function clock */
	pr_debug("APLL %04x DPLL %04x REQ %04x\n", ULPD_APLL_CTRL_REG,
			ULPD_DPLL_CTRL_REG, ULPD_SOFT_REQ_REG);
	ULPD_APLL_CTRL_REG &= ~APLL_NDPLL_SWITCH;
	ULPD_DPLL_CTRL_REG |= DPLL_IOB | DPLL_PLL_ENABLE;
	ULPD_SOFT_REQ_REG |= SOFT_UDC_REQ | SOFT_DPLL_REQ;
	while (!(ULPD_DPLL_CTRL_REG & DPLL_LOCK))
		cpu_relax();

#ifdef	CONFIG_USB_GADGET_OMAP
	if (config->register_dev) {
		udc_device.dev.platform_data = config;
		status = platform_device_register(&udc_device);
		if (status)
			pr_debug("can't register UDC device, %d\n", status);
		/* udc driver gates 48MHz by D+ pullup */
	}
#endif

#if	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	if (config->register_host) {
		ohci_device.dev.platform_data = config;
		status = platform_device_register(&ohci_device);
		if (status)
			pr_debug("can't register OHCI device, %d\n", status);
		/* hcd explicitly gates 48MHz */
	}
#endif

}

#else
static inline void omap_1510_usb_init(struct omap_usb_config *config) {}
#endif

/*-------------------------------------------------------------------------*/

static struct omap_usb_config platform_data;

static int __init
omap_usb_init(void)
{
	const struct omap_usb_config *config;

	config = omap_get_config(OMAP_TAG_USB, struct omap_usb_config);
	if (config == NULL) {
		printk(KERN_ERR "USB: No board-specific platform config found\n");
		return -ENODEV;
	}
	platform_data = *config;

	if (cpu_is_omap730() || cpu_is_omap16xx())
		omap_otg_init(&platform_data);
	else if (cpu_is_omap15xx())
		omap_1510_usb_init(&platform_data);
	else {
		printk(KERN_ERR "USB: No init for your chip yet\n");
		return -ENODEV;
	}
	return 0;
}

subsys_initcall(omap_usb_init);
