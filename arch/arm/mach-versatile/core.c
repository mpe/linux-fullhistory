/*
 *  linux/arch/arm/mach-versatile/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/hardware/amba.h>
#include <asm/hardware/amba_clcd.h>
#include <asm/hardware/icst307.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/mach/mmc.h>

#include "core.h"
#include "clock.h"

/*
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Versatile Vectored Interrupt Controller.
 */
#define VA_VIC_BASE		 IO_ADDRESS(VERSATILE_VIC_BASE)
#define VA_SIC_BASE		 IO_ADDRESS(VERSATILE_SIC_BASE)

static void vic_mask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, VA_VIC_BASE + VIC_IRQ_ENABLE_CLEAR);
}

static void vic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, VA_VIC_BASE + VIC_IRQ_ENABLE);
}

static struct irqchip vic_chip = {
	.ack	= vic_mask_irq,
	.mask	= vic_mask_irq,
	.unmask	= vic_unmask_irq,
};

static void sic_mask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	writel(1 << irq, VA_SIC_BASE + SIC_IRQ_ENABLE_CLEAR);
}

static void sic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	writel(1 << irq, VA_SIC_BASE + SIC_IRQ_ENABLE_SET);
}

static struct irqchip sic_chip = {
	.ack	= sic_mask_irq,
	.mask	= sic_mask_irq,
	.unmask	= sic_unmask_irq,
};

static void
sic_handle_irq(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned long status = readl(VA_SIC_BASE + SIC_IRQ_STATUS);

	if (status == 0) {
		do_bad_IRQ(irq, desc, regs);
		return;
	}

	do {
		irq = ffs(status) - 1;
		status &= ~(1 << irq);

		irq += IRQ_SIC_START;

		desc = irq_desc + irq;
		desc->handle(irq, desc, regs);
	} while (status);
}

#if 1
#define IRQ_MMCI0A	IRQ_VICSOURCE22
#define IRQ_AACI	IRQ_VICSOURCE24
#define IRQ_ETH		IRQ_VICSOURCE25
#define PIC_MASK	0xFFD00000
#else
#define IRQ_MMCI0A	IRQ_SIC_MMCI0A
#define IRQ_AACI	IRQ_SIC_AACI
#define IRQ_ETH		IRQ_SIC_ETH
#define PIC_MASK	0
#endif

void __init versatile_init_irq(void)
{
	unsigned int i, value;

	/* Disable all interrupts initially. */

	writel(0, VA_VIC_BASE + VIC_INT_SELECT);
	writel(0, VA_VIC_BASE + VIC_IRQ_ENABLE);
	writel(~0, VA_VIC_BASE + VIC_IRQ_ENABLE_CLEAR);
	writel(0, VA_VIC_BASE + VIC_IRQ_STATUS);
	writel(0, VA_VIC_BASE + VIC_ITCR);
	writel(~0, VA_VIC_BASE + VIC_IRQ_SOFT_CLEAR);

	/*
	 * Make sure we clear all existing interrupts
	 */
	writel(0, VA_VIC_BASE + VIC_VECT_ADDR);
	for (i = 0; i < 19; i++) {
		value = readl(VA_VIC_BASE + VIC_VECT_ADDR);
		writel(value, VA_VIC_BASE + VIC_VECT_ADDR);
	}

	for (i = 0; i < 16; i++) {
		value = readl(VA_VIC_BASE + VIC_VECT_CNTL0 + (i * 4));
		writel(value | VICVectCntl_Enable | i, VA_VIC_BASE + VIC_VECT_CNTL0 + (i * 4));
	}

	writel(32, VA_VIC_BASE + VIC_DEF_VECT_ADDR);

	for (i = IRQ_VIC_START; i <= IRQ_VIC_END; i++) {
		if (i != IRQ_VICSOURCE31) {
			set_irq_chip(i, &vic_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}

	set_irq_handler(IRQ_VICSOURCE31, sic_handle_irq);
	vic_unmask_irq(IRQ_VICSOURCE31);

	/* Do second interrupt controller */
	writel(~0, VA_SIC_BASE + SIC_IRQ_ENABLE_CLEAR);

	for (i = IRQ_SIC_START; i <= IRQ_SIC_END; i++) {
		if ((PIC_MASK & (1 << (i - IRQ_SIC_START))) == 0) {
			set_irq_chip(i, &sic_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}

	/*
	 * Interrupts on secondary controller from 0 to 8 are routed to
	 * source 31 on PIC.
	 * Interrupts from 21 to 31 are routed directly to the VIC on
	 * the corresponding number on primary controller. This is controlled
	 * by setting PIC_ENABLEx.
	 */
	writel(PIC_MASK, VA_SIC_BASE + SIC_INT_PIC_ENABLE);
}

static struct map_desc versatile_io_desc[] __initdata = {
 { IO_ADDRESS(VERSATILE_SYS_BASE),   VERSATILE_SYS_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_SIC_BASE),   VERSATILE_SIC_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_VIC_BASE),   VERSATILE_VIC_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_SCTL_BASE),  VERSATILE_SCTL_BASE,  SZ_4K * 9,  MT_DEVICE },
#ifdef CONFIG_MACH_VERSATILE_AB
 { IO_ADDRESS(VERSATILE_GPIO0_BASE), VERSATILE_GPIO0_BASE, SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_IB2_BASE),   VERSATILE_IB2_BASE,   SZ_64M,     MT_DEVICE },
#endif
#ifdef CONFIG_DEBUG_LL
 { IO_ADDRESS(VERSATILE_UART0_BASE), VERSATILE_UART0_BASE, SZ_4K,      MT_DEVICE },
#endif
#ifdef FIXME
 { PCI_MEMORY_VADDR,		     PHYS_PCI_MEM_BASE,    SZ_16M,     MT_DEVICE },
 { PCI_CONFIG_VADDR,		     PHYS_PCI_CONFIG_BASE, SZ_16M,     MT_DEVICE },
 { PCI_V3_VADDR,		     PHYS_PCI_V3_BASE,     SZ_512K,    MT_DEVICE },
 { PCI_IO_VADDR,		     PHYS_PCI_IO_BASE,     SZ_64K,     MT_DEVICE },
#endif
};

void __init versatile_map_io(void)
{
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}

#define VERSATILE_REFCOUNTER	(IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_24MHz_OFFSET)

/*
 * This is the Versatile sched_clock implementation.  This has
 * a resolution of 41.7ns, and a maximum value of about 179s.
 */
unsigned long long sched_clock(void)
{
	unsigned long long v;

	v = (unsigned long long)readl(VERSATILE_REFCOUNTER) * 125;
	do_div(v, 3);

	return v;
}


#define VERSATILE_FLASHCTRL    (IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_FLASH_OFFSET)

static int versatile_flash_init(void)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);

	return 0;
}

static void versatile_flash_exit(void)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static void versatile_flash_set_vpp(int on)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	if (on)
		val |= VERSATILE_FLASHPROG_FLVPPEN;
	else
		val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static struct flash_platform_data versatile_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 4,
	.init			= versatile_flash_init,
	.exit			= versatile_flash_exit,
	.set_vpp		= versatile_flash_set_vpp,
};

static struct resource versatile_flash_resource = {
	.start			= VERSATILE_FLASH_BASE,
	.end			= VERSATILE_FLASH_BASE + VERSATILE_FLASH_SIZE,
	.flags			= IORESOURCE_MEM,
};

static struct platform_device versatile_flash_device = {
	.name			= "armflash",
	.id			= 0,
	.dev			= {
		.platform_data	= &versatile_flash_data,
	},
	.num_resources		= 1,
	.resource		= &versatile_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= VERSATILE_ETH_BASE,
		.end		= VERSATILE_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_ETH,
		.end		= IRQ_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#define VERSATILE_SYSMCI	(IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_MCI_OFFSET)

unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(VERSATILE_SYSMCI) & mask;
}

static struct mmc_platform_data mmc0_plat_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};

/*
 * Clock handling
 */
static const struct icst307_params versatile_oscvco_params = {
	.ref		= 24000,
	.vco_max	= 200000,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
};

static void versatile_oscvco_set(struct clk *clk, struct icst307_vco vco)
{
	unsigned long sys_lock = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_LOCK_OFFSET;
#if defined(CONFIG_ARCH_VERSATILE_PB)
	unsigned long sys_osc = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_OSC4_OFFSET;
#elif defined(CONFIG_MACH_VERSATILE_AB)
	unsigned long sys_osc = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_OSC1_OFFSET;
#endif
	u32 val;

	val = readl(sys_osc) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	writel(0xa05f, sys_lock);
	writel(val, sys_osc);
	writel(0, sys_lock);
}

static struct clk versatile_clcd_clk = {
	.name	= "CLCDCLK",
	.params	= &versatile_oscvco_params,
	.setvco = versatile_oscvco_set,
};

/*
 * CLCD support.
 */
#define SYS_CLCD_MODE_MASK	(3 << 0)
#define SYS_CLCD_MODE_888	(0 << 0)
#define SYS_CLCD_MODE_5551	(1 << 0)
#define SYS_CLCD_MODE_565_RLSB	(2 << 0)
#define SYS_CLCD_MODE_565_BLSB	(3 << 0)
#define SYS_CLCD_NLCDIOON	(1 << 2)
#define SYS_CLCD_VDDPOSSWITCH	(1 << 3)
#define SYS_CLCD_PWR3V5SWITCH	(1 << 4)
#define SYS_CLCD_ID_MASK	(0x1f << 8)
#define SYS_CLCD_ID_SANYO_3_8	(0x00 << 8)
#define SYS_CLCD_ID_UNKNOWN_8_4	(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2	(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5	(0x07 << 8)
#define SYS_CLCD_ID_VGA		(0x1f << 8)

static struct clcd_panel vga = {
	.mode		= {
		.name		= "VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 40,
		.right_margin	= 24,
		.upper_margin	= 32,
		.lower_margin	= 11,
		.hsync_len	= 96,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel sanyo_3_8_in = {
	.mode		= {
		.name		= "Sanyo QVGA",
		.refresh	= 116,
		.xres		= 320,
		.yres		= 240,
		.pixclock	= 100000,
		.left_margin	= 6,
		.right_margin	= 6,
		.upper_margin	= 5,
		.lower_margin	= 5,
		.hsync_len	= 6,
		.vsync_len	= 6,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel sanyo_2_5_in = {
	.mode		= {
		.name		= "Sanyo QVGA Portrait",
		.refresh	= 116,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 100000,
		.left_margin	= 20,
		.right_margin	= 10,
		.upper_margin	= 2,
		.lower_margin	= 2,
		.hsync_len	= 10,
		.vsync_len	= 2,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_IVS | TIM2_IHS | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

static struct clcd_panel epson_2_2_in = {
	.mode		= {
		.name		= "Epson QCIF",
		.refresh	= 390,
		.xres		= 176,
		.yres		= 220,
		.pixclock	= 62500,
		.left_margin	= 3,
		.right_margin	= 2,
		.upper_margin	= 1,
		.lower_margin	= 0,
		.hsync_len	= 3,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= TIM2_BCD | TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
};

/*
 * Detect which LCD panel is connected, and return the appropriate
 * clcd_panel structure.  Note: we do not have any information on
 * the required timings for the 8.4in panel, so we presently assume
 * VGA timings.
 */
static struct clcd_panel *versatile_clcd_panel(void)
{
	unsigned long sys_clcd = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	struct clcd_panel *panel = &vga;
	u32 val;

	val = readl(sys_clcd) & SYS_CLCD_ID_MASK;
	if (val == SYS_CLCD_ID_SANYO_3_8)
		panel = &sanyo_3_8_in;
	else if (val == SYS_CLCD_ID_SANYO_2_5)
		panel = &sanyo_2_5_in;
	else if (val == SYS_CLCD_ID_EPSON_2_2)
		panel = &epson_2_2_in;
	else if (val == SYS_CLCD_ID_VGA)
		panel = &vga;
	else {
		printk(KERN_ERR "CLCD: unknown LCD panel ID 0x%08x, using VGA\n",
			val);
		panel = &vga;
	}

	return panel;
}

/*
 * Disable all display connectors on the interface module.
 */
static void versatile_clcd_disable(struct clcd_fb *fb)
{
	unsigned long sys_clcd = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

#ifdef CONFIG_MACH_VERSATILE_AB
	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light off
	 */
	if (fb->panel == &sanyo_2_5_in) {
		unsigned long versatile_ib2_ctrl = IO_ADDRESS(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl &= ~0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
#endif
}

/*
 * Enable the relevant connector on the interface module.
 */
static void versatile_clcd_enable(struct clcd_fb *fb)
{
	unsigned long sys_clcd = IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_CLCD_OFFSET;
	u32 val;

	val = readl(sys_clcd);
	val &= ~SYS_CLCD_MODE_MASK;

	switch (fb->fb.var.green.length) {
	case 5:
		val |= SYS_CLCD_MODE_5551;
		break;
	case 6:
		val |= SYS_CLCD_MODE_565_BLSB;
		break;
	case 8:
		val |= SYS_CLCD_MODE_888;
		break;
	}

	/*
	 * Set the MUX
	 */
	writel(val, sys_clcd);

	/*
	 * And now enable the PSUs
	 */
	val |= SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH;
	writel(val, sys_clcd);

#ifdef CONFIG_MACH_VERSATILE_AB
	/*
	 * If the LCD is Sanyo 2x5 in on the IB2 board, turn the back-light on
	 */
	if (fb->panel == &sanyo_2_5_in) {
		unsigned long versatile_ib2_ctrl = IO_ADDRESS(VERSATILE_IB2_CTRL);
		unsigned long ctrl;

		ctrl = readl(versatile_ib2_ctrl);
		ctrl |= 0x01;
		writel(ctrl, versatile_ib2_ctrl);
	}
#endif
}

static unsigned long framesize = SZ_1M;

static int versatile_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->panel		= versatile_clcd_panel();

	fb->fb.screen_base = dma_alloc_writecombine(&fb->dev->dev, framesize,
						    &dma, GFP_KERNEL);
	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start	= dma;
	fb->fb.fix.smem_len	= framesize;

	return 0;
}

static int versatile_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
				     fb->fb.screen_base,
				     fb->fb.fix.smem_start,
				     fb->fb.fix.smem_len);
}

static void versatile_clcd_remove(struct clcd_fb *fb)
{
	dma_free_writecombine(&fb->dev->dev, fb->fb.fix.smem_len,
			      fb->fb.screen_base, fb->fb.fix.smem_start);
}

static struct clcd_board clcd_plat_data = {
	.name		= "Versatile",
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.disable	= versatile_clcd_disable,
	.enable		= versatile_clcd_enable,
	.setup		= versatile_clcd_setup,
	.mmap		= versatile_clcd_mmap,
	.remove		= versatile_clcd_remove,
};

#define AACI_IRQ	{ IRQ_AACI, NO_IRQ }
#define AACI_DMA	{ 0x80, 0x81 }
#define MMCI0_IRQ	{ IRQ_MMCI0A,IRQ_SIC_MMCI0B }
#define MMCI0_DMA	{ 0x84, 0 }
#define KMI0_IRQ	{ IRQ_SIC_KMI0, NO_IRQ }
#define KMI0_DMA	{ 0, 0 }
#define KMI1_IRQ	{ IRQ_SIC_KMI1, NO_IRQ }
#define KMI1_DMA	{ 0, 0 }

/*
 * These devices are connected directly to the multi-layer AHB switch
 */
#define SMC_IRQ		{ NO_IRQ, NO_IRQ }
#define SMC_DMA		{ 0, 0 }
#define MPMC_IRQ	{ NO_IRQ, NO_IRQ }
#define MPMC_DMA	{ 0, 0 }
#define CLCD_IRQ	{ IRQ_CLCDINT, NO_IRQ }
#define CLCD_DMA	{ 0, 0 }
#define DMAC_IRQ	{ IRQ_DMAINT, NO_IRQ }
#define DMAC_DMA	{ 0, 0 }

/*
 * These devices are connected via the core APB bridge
 */
#define SCTL_IRQ	{ NO_IRQ, NO_IRQ }
#define SCTL_DMA	{ 0, 0 }
#define WATCHDOG_IRQ	{ IRQ_WDOGINT, NO_IRQ }
#define WATCHDOG_DMA	{ 0, 0 }
#define GPIO0_IRQ	{ IRQ_GPIOINT0, NO_IRQ }
#define GPIO0_DMA	{ 0, 0 }
#define GPIO1_IRQ	{ IRQ_GPIOINT1, NO_IRQ }
#define GPIO1_DMA	{ 0, 0 }
#define RTC_IRQ		{ IRQ_RTCINT, NO_IRQ }
#define RTC_DMA		{ 0, 0 }

/*
 * These devices are connected via the DMA APB bridge
 */
#define SCI_IRQ		{ IRQ_SCIINT, NO_IRQ }
#define SCI_DMA		{ 7, 6 }
#define UART0_IRQ	{ IRQ_UARTINT0, NO_IRQ }
#define UART0_DMA	{ 15, 14 }
#define UART1_IRQ	{ IRQ_UARTINT1, NO_IRQ }
#define UART1_DMA	{ 13, 12 }
#define UART2_IRQ	{ IRQ_UARTINT2, NO_IRQ }
#define UART2_DMA	{ 11, 10 }
#define SSP_IRQ		{ IRQ_SSPINT, NO_IRQ }
#define SSP_DMA		{ 9, 8 }

/* FPGA Primecells */
AMBA_DEVICE(aaci,  "fpga:04", AACI,     NULL);
AMBA_DEVICE(mmc0,  "fpga:05", MMCI0,    &mmc0_plat_data);
AMBA_DEVICE(kmi0,  "fpga:06", KMI0,     NULL);
AMBA_DEVICE(kmi1,  "fpga:07", KMI1,     NULL);

/* DevChip Primecells */
AMBA_DEVICE(smc,   "dev:00",  SMC,      NULL);
AMBA_DEVICE(mpmc,  "dev:10",  MPMC,     NULL);
AMBA_DEVICE(clcd,  "dev:20",  CLCD,     &clcd_plat_data);
AMBA_DEVICE(dmac,  "dev:30",  DMAC,     NULL);
AMBA_DEVICE(sctl,  "dev:e0",  SCTL,     NULL);
AMBA_DEVICE(wdog,  "dev:e1",  WATCHDOG, NULL);
AMBA_DEVICE(gpio0, "dev:e4",  GPIO0,    NULL);
AMBA_DEVICE(gpio1, "dev:e5",  GPIO1,    NULL);
AMBA_DEVICE(rtc,   "dev:e8",  RTC,      NULL);
AMBA_DEVICE(sci0,  "dev:f0",  SCI,      NULL);
AMBA_DEVICE(uart0, "dev:f1",  UART0,    NULL);
AMBA_DEVICE(uart1, "dev:f2",  UART1,    NULL);
AMBA_DEVICE(uart2, "dev:f3",  UART2,    NULL);
AMBA_DEVICE(ssp0,  "dev:f4",  SSP,      NULL);

static struct amba_device *amba_devs[] __initdata = {
	&dmac_device,
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&smc_device,
	&mpmc_device,
	&clcd_device,
	&sctl_device,
	&wdog_device,
	&gpio0_device,
	&gpio1_device,
	&rtc_device,
	&sci0_device,
	&ssp0_device,
	&aaci_device,
	&mmc0_device,
	&kmi0_device,
	&kmi1_device,
};

#ifdef CONFIG_LEDS
#define VA_LEDS_BASE (IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_LED_OFFSET)

static void versatile_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(VA_LEDS_BASE);

	switch (ledevt) {
	case led_idle_start:
		val = val & ~VERSATILE_SYS_LED0;
		break;

	case led_idle_end:
		val = val | VERSATILE_SYS_LED0;
		break;

	case led_timer:
		val = val ^ VERSATILE_SYS_LED1;
		break;

	case led_halted:
		val = 0;
		break;

	default:
		break;
	}

	writel(val, VA_LEDS_BASE);
	local_irq_restore(flags);
}
#endif	/* CONFIG_LEDS */

void __init versatile_init(void)
{
	int i;

	clk_register(&versatile_clcd_clk);

	platform_device_register(&versatile_flash_device);
	platform_device_register(&smc91x_device);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

#ifdef CONFIG_LEDS
	leds_event = versatile_leds_event;
#endif
}

/*
 * Where is the timer (VA)?
 */
#define TIMER0_VA_BASE		 IO_ADDRESS(VERSATILE_TIMER0_1_BASE)
#define TIMER1_VA_BASE		(IO_ADDRESS(VERSATILE_TIMER0_1_BASE) + 0x20)
#define TIMER2_VA_BASE		 IO_ADDRESS(VERSATILE_TIMER2_3_BASE)
#define TIMER3_VA_BASE		(IO_ADDRESS(VERSATILE_TIMER2_3_BASE) + 0x20)
#define VA_IC_BASE		 IO_ADDRESS(VERSATILE_VIC_BASE) 

/*
 * How long is the timer interval?
 */
#define TIMER_INTERVAL	(TICKS_PER_uSEC * mSEC_10)
#if TIMER_INTERVAL >= 0x100000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 8)		/* Divide by 256 */
#define TIMER_CTRL	0x88				/* Enable, Clock / 256 */
#define TICKS2USECS(x)	(256 * (x) / TICKS_PER_uSEC)
#elif TIMER_INTERVAL >= 0x10000
#define TIMER_RELOAD	(TIMER_INTERVAL >> 4)		/* Divide by 16 */
#define TIMER_CTRL	0x84				/* Enable, Clock / 16 */
#define TICKS2USECS(x)	(16 * (x) / TICKS_PER_uSEC)
#else
#define TIMER_RELOAD	(TIMER_INTERVAL)
#define TIMER_CTRL	0x80				/* Enable */
#define TICKS2USECS(x)	((x) / TICKS_PER_uSEC)
#endif

#define TIMER_CTRL_IE	(1 << 5)	/* Interrupt Enable */

/*
 * What does it look like?
 */
typedef struct TimerStruct {
	unsigned long TimerLoad;
	unsigned long TimerValue;
	unsigned long TimerControl;
	unsigned long TimerClear;
} TimerStruct_t;

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long versatile_gettimeoffset(void)
{
	volatile TimerStruct_t *timer0 = (TimerStruct_t *)TIMER0_VA_BASE;
	unsigned long ticks1, ticks2, status;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for
	 * an interrupt.  We get around this by ensuring that the
	 * counter has not reloaded between our two reads.
	 */
	ticks2 = timer0->TimerValue & 0xffff;
	do {
		ticks1 = ticks2;
		status = __raw_readl(VA_IC_BASE + VIC_IRQ_RAW_STATUS);
		ticks2 = timer0->TimerValue & 0xffff;
	} while (ticks2 > ticks1);

	/*
	 * Number of ticks since last interrupt.
	 */
	ticks1 = TIMER_RELOAD - ticks2;

	/*
	 * Interrupt pending?  If so, we've reloaded once already.
	 *
	 * FIXME: Need to check this is effectively timer 0 that expires
	 */
	if (status & IRQMASK_TIMERINT0_1)
		ticks1 += TIMER_RELOAD;

	/*
	 * Convert the ticks to usecs
	 */
	return TICKS2USECS(ticks1);
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t versatile_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile TimerStruct_t *timer0 = (volatile TimerStruct_t *)TIMER0_VA_BASE;

	write_seqlock(&xtime_lock);

	// ...clear the interrupt
	timer0->TimerClear = 1;

	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction versatile_timer_irq = {
	.name		= "Versatile Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= versatile_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init versatile_timer_init(void)
{
	volatile TimerStruct_t *timer0 = (volatile TimerStruct_t *)TIMER0_VA_BASE;
	volatile TimerStruct_t *timer1 = (volatile TimerStruct_t *)TIMER1_VA_BASE;
	volatile TimerStruct_t *timer2 = (volatile TimerStruct_t *)TIMER2_VA_BASE;
	volatile TimerStruct_t *timer3 = (volatile TimerStruct_t *)TIMER3_VA_BASE;

	/* 
	 * set clock frequency: 
	 *	VERSATILE_REFCLK is 32KHz
	 *	VERSATILE_TIMCLK is 1MHz
	 */
	*(volatile unsigned int *)IO_ADDRESS(VERSATILE_SCTL_BASE) |= 
	  ((VERSATILE_TIMCLK << VERSATILE_TIMER1_EnSel) | (VERSATILE_TIMCLK << VERSATILE_TIMER2_EnSel) | 
	   (VERSATILE_TIMCLK << VERSATILE_TIMER3_EnSel) | (VERSATILE_TIMCLK << VERSATILE_TIMER4_EnSel));

	/*
	 * Initialise to a known state (all timers off)
	 */
	timer0->TimerControl = 0;
	timer1->TimerControl = 0;
	timer2->TimerControl = 0;
	timer3->TimerControl = 0;

	timer0->TimerLoad    = TIMER_RELOAD;
	timer0->TimerValue   = TIMER_RELOAD;
	timer0->TimerControl = TIMER_CTRL | 0x40 | TIMER_CTRL_IE;  /* periodic + IE */

	/* 
	 * Make irqs happen for the system timer
	 */
	setup_irq(IRQ_TIMERINT0_1, &versatile_timer_irq);
}

struct sys_timer versatile_timer = {
	.init		= versatile_timer_init,
	.offset		= versatile_gettimeoffset,
};
