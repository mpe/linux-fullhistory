/*
 *  linux/arch/arm/mach-pxa/mainstone.c
 *
 *  Support for the Intel HCDDBBVA0 Development Platform.
 *  (go figure how they came up with such name...)
 *
 *  Author:	Nicolas Pitre
 *  Created:	Nov 05, 2002
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mainstone.h>
#include <asm/arch/audio.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>

#include "generic.h"


static unsigned long mainstone_irq_enabled;

static void mainstone_mask_irq(unsigned int irq)
{
	int mainstone_irq = (irq - MAINSTONE_IRQ(0));
	MST_INTMSKENA = (mainstone_irq_enabled &= ~(1 << mainstone_irq));
}

static void mainstone_unmask_irq(unsigned int irq)
{
	int mainstone_irq = (irq - MAINSTONE_IRQ(0));
	/* the irq can be acknowledged only if deasserted, so it's done here */
	MST_INTSETCLR &= ~(1 << mainstone_irq);
	MST_INTMSKENA = (mainstone_irq_enabled |= (1 << mainstone_irq));
}

static struct irqchip mainstone_irq_chip = {
	.ack		= mainstone_mask_irq,
	.mask		= mainstone_mask_irq,
	.unmask		= mainstone_unmask_irq,
};


static void mainstone_irq_handler(unsigned int irq, struct irqdesc *desc,
				  struct pt_regs *regs)
{
	unsigned long pending = MST_INTSETCLR & mainstone_irq_enabled;
	do {
		GEDR(0) = GPIO_bit(0);  /* clear useless edge notification */
		if (likely(pending)) {
			irq = MAINSTONE_IRQ(0) + __ffs(pending);
			desc = irq_desc + irq;
			desc->handle(irq, desc, regs);
		}
		pending = MST_INTSETCLR & mainstone_irq_enabled;
	} while (pending);
}

static void __init mainstone_init_irq(void)
{
	int irq;

	pxa_init_irq();

	/* setup extra Mainstone irqs */
	for(irq = MAINSTONE_IRQ(0); irq <= MAINSTONE_IRQ(15); irq++) {
		set_irq_chip(irq, &mainstone_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}
	set_irq_flags(MAINSTONE_IRQ(8), 0);
	set_irq_flags(MAINSTONE_IRQ(12), 0);

	MST_INTMSKENA = 0;
	MST_INTSETCLR = 0;

	set_irq_chained_handler(IRQ_GPIO(0), mainstone_irq_handler);
	set_irq_type(IRQ_GPIO(0), IRQT_FALLING);
}


static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (MST_ETH_PHYS + 0x300),
		.end	= (MST_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MAINSTONE_IRQ(3),
		.end	= MAINSTONE_IRQ(3),
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static int mst_audio_startup(snd_pcm_substream_t *substream, void *priv)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MST_MSCWR2 &= ~MST_MSCWR2_AC97_SPKROFF;
	return 0;
}

static void mst_audio_shutdown(snd_pcm_substream_t *substream, void *priv)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MST_MSCWR2 |= MST_MSCWR2_AC97_SPKROFF;
}

static long mst_audio_suspend_mask;

static void mst_audio_suspend(void *priv)
{
	mst_audio_suspend_mask = MST_MSCWR2;
	MST_MSCWR2 |= MST_MSCWR2_AC97_SPKROFF;
}

static void mst_audio_resume(void *priv)
{
	MST_MSCWR2 &= mst_audio_suspend_mask | ~MST_MSCWR2_AC97_SPKROFF;
}

static pxa2xx_audio_ops_t mst_audio_ops = {
	.startup	= mst_audio_startup,
	.shutdown	= mst_audio_shutdown,
	.suspend	= mst_audio_suspend,
	.resume		= mst_audio_resume,
};

static struct platform_device mst_audio_device = {
	.name		= "pxa2xx-ac97",
	.id		= -1,
	.dev		= { .platform_data = &mst_audio_ops },
};

static void mainstone_backlight_power(int on)
{
	if (on) {
		pxa_gpio_mode(GPIO16_PWM0_MD);
		pxa_set_cken(CKEN0_PWM0, 1);
		PWM_CTRL0 = 0;
		PWM_PWDUTY0 = 0x3ff;
		PWM_PERVAL0 = 0x3ff;
	} else {
		PWM_CTRL0 = 0;
		PWM_PWDUTY0 = 0x0;
		PWM_PERVAL0 = 0x3FF;
		pxa_set_cken(CKEN0_PWM0, 0);
	}
}

static struct pxafb_mach_info toshiba_ltm04c380k __initdata = {
	.pixclock		= 50000,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 1,
	.left_margin		= 0x9f,
	.right_margin		= 1,
	.vsync_len		= 44,
	.upper_margin		= 0,
	.lower_margin		= 0,
	.sync			= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
	.lccr0			= LCCR0_Act,
	.lccr3			= LCCR3_PCP,
	.pxafb_backlight_power	= mainstone_backlight_power,
};

static struct pxafb_mach_info toshiba_ltm035a776c __initdata = {
	.pixclock		= 110000,
	.xres			= 240,
	.yres			= 320,
	.bpp			= 16,
	.hsync_len		= 4,
	.left_margin		= 8,
	.right_margin		= 20,
	.vsync_len		= 3,
	.upper_margin		= 1,
	.lower_margin		= 10,
	.sync			= FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT,
	.lccr0			= LCCR0_Act,
	.lccr3			= LCCR3_PCP,
	.pxafb_backlight_power	= mainstone_backlight_power,
};

static int mainstone_mci_init(struct device *dev, irqreturn_t (*mstone_detect_int)(int, void *, struct pt_regs *), void *data)
{
	int err;

	/*
	 * setup GPIO for PXA27x MMC controller
	 */
	pxa_gpio_mode(GPIO32_MMCCLK_MD);
	pxa_gpio_mode(GPIO112_MMCCMD_MD);
	pxa_gpio_mode(GPIO92_MMCDAT0_MD);
	pxa_gpio_mode(GPIO109_MMCDAT1_MD);
	pxa_gpio_mode(GPIO110_MMCDAT2_MD);
	pxa_gpio_mode(GPIO111_MMCDAT3_MD);

	/* make sure SD/Memory Stick multiplexer's signals
	 * are routed to MMC controller
	 */
	MST_MSCWR1 &= ~MST_MSCWR1_MS_SEL;

	err = request_irq(MAINSTONE_MMC_IRQ, mstone_detect_int, SA_INTERRUPT,
			     "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "mainstone_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}

	return 0;
}

static void mainstone_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		printk(KERN_DEBUG "%s: on\n", __FUNCTION__);
		MST_MSCWR1 |= MST_MSCWR1_MMC_ON;
		MST_MSCWR1 &= ~MST_MSCWR1_MS_SEL;
	} else {
		printk(KERN_DEBUG "%s: off\n", __FUNCTION__);
		MST_MSCWR1 &= ~MST_MSCWR1_MMC_ON;
	}
}

static void mainstone_mci_exit(struct device *dev, void *data)
{
	free_irq(MAINSTONE_MMC_IRQ, data);
}

static struct pxamci_platform_data mainstone_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= mainstone_mci_init,
	.setpower 	= mainstone_mci_setpower,
	.exit		= mainstone_mci_exit,
};

static void __init mainstone_init(void)
{
	/*
	 * On Mainstone, we route AC97_SYSCLK via GPIO45 to
	 * the audio daughter card
	 */
	pxa_gpio_mode(GPIO45_SYSCLK_AC97_MD);

	platform_device_register(&smc91x_device);
	platform_device_register(&mst_audio_device);

	/* reading Mainstone's "Virtual Configuration Register"
	   might be handy to select LCD type here */
	if (0)
		set_pxa_fb_info(&toshiba_ltm04c380k);
	else
		set_pxa_fb_info(&toshiba_ltm035a776c);

	pxa_set_mci_info(&mainstone_mci_platform_data);
}


static struct map_desc mainstone_io_desc[] __initdata = {
  { MST_FPGA_VIRT, MST_FPGA_PHYS, 0x00100000, MT_DEVICE }, /* CPLD */
};

static void __init mainstone_map_io(void)
{
	pxa_map_io();
	iotable_init(mainstone_io_desc, ARRAY_SIZE(mainstone_io_desc));

	/* initialize sleep mode regs (wake-up sources, etc) */
	PGSR0 = 0x00008800;
	PGSR1 = 0x00000002;
	PGSR2 = 0x0001FC00;
	PGSR3 = 0x00001F81;
	PWER  = 0xC0000002;
	PRER  = 0x00000002;
	PFER  = 0x00000002;
}

MACHINE_START(MAINSTONE, "Intel HCDDBBVA0 Development Platform (aka Mainstone)")
	MAINTAINER("MontaVista Software Inc.")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	MAPIO(mainstone_map_io)
	INITIRQ(mainstone_init_irq)
	.timer		= &pxa_timer,
	INIT_MACHINE(mainstone_init)
MACHINE_END
