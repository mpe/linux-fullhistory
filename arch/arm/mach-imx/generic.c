/*
 *  arch/arm/mach-imx/generic.c
 *
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX machines
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
 *
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/hardware.h>

#include <asm/mach/map.h>

void imx_gpio_mode(int gpio_mode)
{
	unsigned int pin = gpio_mode & GPIO_PIN_MASK;
	unsigned int port = (gpio_mode & GPIO_PORT_MASK) >> 5;
	unsigned int ocr = (gpio_mode & GPIO_OCR_MASK) >> 10;
	unsigned int tmp;

	/* Pullup enable */
	if(gpio_mode & GPIO_PUEN)
		PUEN(port) |= (1<<pin);
	else
		PUEN(port) &= ~(1<<pin);

	/* Data direction */
	if(gpio_mode & GPIO_OUT)
		DDIR(port) |= 1<<pin;
	else
		DDIR(port) &= ~(1<<pin);

	/* Primary / alternate function */
	if(gpio_mode & GPIO_AF)
		GPR(port) |= (1<<pin);
	else
		GPR(port) &= ~(1<<pin);

	/* use as gpio? */
	if( ocr == 3 )
		GIUS(port) |= (1<<pin);
	else
		GIUS(port) &= ~(1<<pin);

	/* Output / input configuration */
	/* FIXME: I'm not very sure about OCR and ICONF, someone
	 * should have a look over it
	 */
	if(pin<16) {
		tmp = OCR1(port);
		tmp &= ~( 3<<(pin*2));
		tmp |= (ocr << (pin*2));
		OCR1(port) = tmp;

		if( gpio_mode &	GPIO_AOUT )
			ICONFA1(port) &= ~( 3<<(pin*2));
		if( gpio_mode &	GPIO_BOUT )
			ICONFB1(port) &= ~( 3<<(pin*2));
	} else {
		tmp = OCR2(port);
		tmp &= ~( 3<<((pin-16)*2));
		tmp |= (ocr << ((pin-16)*2));
		OCR2(port) = tmp;

		if( gpio_mode &	GPIO_AOUT )
			ICONFA2(port) &= ~( 3<<((pin-16)*2));
		if( gpio_mode &	GPIO_BOUT )
			ICONFB2(port) &= ~( 3<<((pin-16)*2));
	}
}

EXPORT_SYMBOL(imx_gpio_mode);

/*
 *  get the system pll clock in Hz
 *
 *                  mfi + mfn / (mfd +1)
 *  f = 2 * f_ref * --------------------
 *                        pd + 1
 */
static unsigned int imx_decode_pll(unsigned int pll)
{
	u32 mfi = (pll >> 10) & 0xf;
	u32 mfn = pll & 0x3ff;
	u32 mfd = (pll >> 16) & 0x3ff;
	u32 pd =  (pll >> 26) & 0xf;
	u32 f_ref = (CSCR & CSCR_SYSTEM_SEL) ? 16000000 : (CLK32 * 512);

	mfi = mfi <= 5 ? 5 : mfi;

	return (2 * (f_ref>>10) * ( (mfi<<10) + (mfn<<10) / (mfd+1) )) / (pd+1);
}

unsigned int imx_get_system_clk(void)
{
	return imx_decode_pll(SPCTL0);
}
EXPORT_SYMBOL(imx_get_system_clk);

unsigned int imx_get_mcu_clk(void)
{
	return imx_decode_pll(MPCTL0);
}
EXPORT_SYMBOL(imx_get_mcu_clk);

/*
 *  get peripheral clock 1 ( UART[12], Timer[12], PWM )
 */
unsigned int imx_get_perclk1(void)
{
	return imx_get_system_clk() / (((PCDR) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_perclk1);

/*
 *  get peripheral clock 2 ( LCD, SD, SPI[12] )
 */
unsigned int imx_get_perclk2(void)
{
	return imx_get_system_clk() / (((PCDR>>4) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_perclk2);

/*
 *  get peripheral clock 3 ( SSI )
 */
unsigned int imx_get_perclk3(void)
{
	return imx_get_system_clk() / (((PCDR>>16) & 0x7f)+1);
}
EXPORT_SYMBOL(imx_get_perclk3);

/*
 *  get hclk ( SDRAM, CSI, Memory Stick, I2C, DMA )
 */
unsigned int imx_get_hclk(void)
{
	return imx_get_system_clk() / (((CSCR>>10) & 0xf)+1);
}
EXPORT_SYMBOL(imx_get_hclk);

static struct resource imx_mmc_resources[] = {
	[0] = {
		.start	= 0x00214000,
		.end	= 0x002140FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (SDHC_INT),
		.end	= (SDHC_INT),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imx_mmc_device = {
	.name		= "imx-mmc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_mmc_resources),
	.resource	= imx_mmc_resources,
};

static struct resource imx_uart1_resources[] = {
	[0] = {
		.start	= 0x00206000,
		.end	= 0x002060FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (UART1_MINT_RX),
		.end	= (UART1_MINT_RX),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= (UART1_MINT_TX),
		.end	= (UART1_MINT_TX),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imx_uart1_device = {
	.name		= "imx-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imx_uart1_resources),
	.resource	= imx_uart1_resources,
};

static struct resource imx_uart2_resources[] = {
	[0] = {
		.start	= 0x00207000,
		.end	= 0x002070FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (UART2_MINT_RX),
		.end	= (UART2_MINT_RX),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= (UART2_MINT_TX),
		.end	= (UART2_MINT_TX),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imx_uart2_device = {
	.name		= "imx-uart",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(imx_uart2_resources),
	.resource	= imx_uart2_resources,
};

static struct resource imxfb_resources[] = {
	[0] = {
		.start	= 0x00205000,
		.end	= 0x002050FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LCDC_INT,
		.end	= LCDC_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device imxfb_device = {
	.name		= "imx-fb",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(imxfb_resources),
	.resource	= imxfb_resources,
};

static struct platform_device *devices[] __initdata = {
	&imx_mmc_device,
	&imxfb_device,
	&imx_uart1_device,
	&imx_uart2_device,
};

static struct map_desc imx_io_desc[] __initdata = {
	/* virtual     physical    length      type */
	{IMX_IO_BASE, IMX_IO_PHYS, IMX_IO_SIZE, MT_DEVICE},
};

void __init
imx_map_io(void)
{
	iotable_init(imx_io_desc, ARRAY_SIZE(imx_io_desc));
}

static int __init imx_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(imx_init);
