/*
 * arch/arm/mach-ixp2000/ixdp2x01.c
 *
 * Code common to Intel IXDP2401 and IXDP2801 platforms
 *
 * Original Author: Andrzej Mialwoski <andrzej.mialwoski@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002-2003 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

/*************************************************************************
 * IXDP2x01 IRQ Handling
 *************************************************************************/
static void ixdp2x01_irq_mask(unsigned int irq)
{
	*IXDP2X01_INT_MASK_SET_REG = IXP2000_BOARD_IRQ_MASK(irq);
}

static void ixdp2x01_irq_unmask(unsigned int irq)
{
	*IXDP2X01_INT_MASK_CLR_REG = IXP2000_BOARD_IRQ_MASK(irq);
}

static u32 valid_irq_mask;

static void ixdp2x01_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	u32 ex_interrupt;
	int i;

	desc->chip->mask(irq);

	ex_interrupt = *IXDP2X01_INT_STAT_REG & valid_irq_mask;

	if (!ex_interrupt) {
		printk(KERN_ERR "Spurious IXDP2X01 CPLD interrupt!\n");
		return;
	}

	for (i = 0; i < IXP2000_BOARD_IRQS; i++) {
		if (ex_interrupt & (1 << i)) {
			struct irqdesc *cpld_desc;
			int cpld_irq = IXP2000_BOARD_IRQ(0) + i;
			cpld_desc = irq_desc + cpld_irq;
			cpld_desc->handle(cpld_irq, cpld_desc, regs);
		}
	}

	desc->chip->unmask(irq);
}

static struct irqchip ixdp2x01_irq_chip = {
	.mask	= ixdp2x01_irq_mask,
	.ack	= ixdp2x01_irq_mask,
	.unmask	= ixdp2x01_irq_unmask
};

/*
 * We only do anything if we are the master NPU on the board.
 * The slave NPU only has the ethernet chip going directly to
 * the PCIB interrupt input.
 */
void __init ixdp2x01_init_irq(void)
{
	int irq = 0;

	/* initialize chip specific interrupts */
	ixp2000_init_irq();

	if (machine_is_ixdp2401())
		valid_irq_mask = IXDP2401_VALID_IRQ_MASK;
	else
		valid_irq_mask = IXDP2801_VALID_IRQ_MASK;

	/* Mask all interrupts from CPLD, disable simulation */
	*IXDP2X01_INT_MASK_SET_REG = 0xffffffff;
	*IXDP2X01_INT_SIM_REG = 0;

	for (irq = NR_IXP2000_IRQS; irq < NR_IXDP2X01_IRQS; irq++) {
		if (irq & valid_irq_mask) {
			set_irq_chip(irq, &ixdp2x01_irq_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, IRQF_VALID);
		} else {
			set_irq_flags(irq, 0);
		}
	}

	/* Hook into PCI interrupts */
	set_irq_chained_handler(IRQ_IXP2000_PCIB, &ixdp2x01_irq_handler);
}


/*************************************************************************
 * IXDP2x01 memory map and serial ports
 *************************************************************************/
static struct map_desc ixdp2x01_io_desc __initdata = {
	.virtual	= IXDP2X01_VIRT_CPLD_BASE, 
	.physical	= IXDP2X01_PHYS_CPLD_BASE,
	.length		= IXDP2X01_CPLD_REGION_SIZE,
	.type		= MT_DEVICE
};

static struct uart_port ixdp2x01_serial_ports[2] = {
	{
		.membase	= (char *)(IXDP2X01_UART1_VIRT_BASE),
		.mapbase	= (unsigned long)IXDP2X01_UART1_PHYS_BASE,
		.irq		= IRQ_IXDP2X01_UART1,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= IXDP2X01_UART_CLK,
		.line		= 1,
		.type		= PORT_16550A,
		.fifosize	= 16
	}, {
		.membase	= (char *)(IXDP2X01_UART2_VIRT_BASE),
		.mapbase	= (unsigned long)IXDP2X01_UART2_PHYS_BASE,
		.irq		= IRQ_IXDP2X01_UART2,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= IXDP2X01_UART_CLK,
		.line		= 2,
		.type		= PORT_16550A,
		.fifosize	= 16
	}, 
};

static void __init ixdp2x01_map_io(void)
{
	ixp2000_map_io();	

	iotable_init(&ixdp2x01_io_desc, 1);

	early_serial_setup(&ixdp2x01_serial_ports[0]);
	early_serial_setup(&ixdp2x01_serial_ports[1]);
}


/*************************************************************************
 * IXDP2x01 timer tick configuration
 *************************************************************************/
static unsigned int ixdp2x01_clock;

static int __init ixdp2x01_clock_setup(char *str)
{
	ixdp2x01_clock = simple_strtoul(str, NULL, 10);

	return 1;
}

__setup("ixdp2x01_clock=", ixdp2x01_clock_setup);

static void __init ixdp2x01_init_time(void)
{
	if (!ixdp2x01_clock)
		ixdp2x01_clock = 50000000;

	ixp2000_init_time(ixdp2x01_clock);
}

/*************************************************************************
 * IXDP2x01 PCI
 *************************************************************************/
void __init ixdp2x01_pci_preinit(void)
{
	ixp2000_reg_write(IXP2000_PCI_ADDR_EXT, 0x00000000);
	ixp2000_pci_preinit();
}

#define DEVPIN(dev, pin) ((pin) | ((dev) << 3))

static int __init ixdp2x01_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	u8 bus = dev->bus->number;
	u32 devpin = DEVPIN(PCI_SLOT(dev->devfn), pin);
	struct pci_bus *tmp_bus = dev->bus;

	/* Primary bus, no interrupts here */
	if (bus == 0) {
		return -1;
	}

	/* Lookup first leaf in bus tree */
	while ((tmp_bus->parent != NULL) && (tmp_bus->parent->parent != NULL)) {
		tmp_bus = tmp_bus->parent;
	}

	/* Select between known bridges */
	switch (tmp_bus->self->devfn | (tmp_bus->self->bus->number << 8)) {
	/* Device is located after first MB bridge */
	case 0x0008:
		if (tmp_bus == dev->bus) {
			/* Device is located directy after first MB bridge */
			switch (devpin) {
			case DEVPIN(1, 1):	/* Onboard 82546 ch 0 */
				if (machine_is_ixdp2401())
					return IRQ_IXDP2401_INTA_82546;
				return -1;
			case DEVPIN(1, 2):	/* Onboard 82546 ch 1 */
				if (machine_is_ixdp2401())
					return IRQ_IXDP2401_INTB_82546;
				return -1;
			case DEVPIN(0, 1):	/* PMC INTA# */
				return IRQ_IXDP2X01_SPCI_PMC_INTA;
			case DEVPIN(0, 2):	/* PMC INTB# */
				return IRQ_IXDP2X01_SPCI_PMC_INTB;
			case DEVPIN(0, 3):	/* PMC INTC# */
				return IRQ_IXDP2X01_SPCI_PMC_INTC;
			case DEVPIN(0, 4):	/* PMC INTD# */
				return IRQ_IXDP2X01_SPCI_PMC_INTD;
			}
		}
		break;
	case 0x0010:
		if (tmp_bus == dev->bus) {
			/* Device is located directy after second MB bridge */
			/* Secondary bus of second bridge */
			switch (devpin) {
			case DEVPIN(0, 1):	/* DB#0 */
				return IRQ_IXDP2X01_SPCI_DB_0;
			case DEVPIN(1, 1):	/* DB#1 */
				return IRQ_IXDP2X01_SPCI_DB_1;
			}
		} else {
			/* Device is located indirectly after second MB bridge */
			/* Not supported now */
		}
		break;
	}

	return -1;
}


static int ixdp2x01_pci_setup(int nr, struct pci_sys_data *sys)
{
	sys->mem_offset = 0xe0000000;

	if (machine_is_ixdp2801())
		sys->mem_offset -= ((*IXP2000_PCI_ADDR_EXT & 0xE000) << 16);

	return ixp2000_pci_setup(nr, sys);
}

struct hw_pci ixdp2x01_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= ixdp2x01_pci_setup,
	.preinit	= ixdp2x01_pci_preinit,
	.scan		= ixp2000_pci_scan_bus,
	.map_irq	= ixdp2x01_pci_map_irq,
};

int __init ixdp2x01_pci_init(void)
{

	pci_common_init(&ixdp2x01_pci);
	return 0;
}

subsys_initcall(ixdp2x01_pci_init);

/*************************************************************************
 * IXDP2x01 Machine Intialization
 *************************************************************************/
static struct flash_platform_data ixdp2x01_flash_platform_data = {
	.map_name	= "cfi_probe",
	.width		= 1,
};

static unsigned long ixdp2x01_flash_bank_setup(unsigned long ofs)
{
	*IXDP2X01_CPLD_FLASH_REG = 
		((ofs >> IXDP2X01_FLASH_WINDOW_BITS) | IXDP2X01_CPLD_FLASH_INTERN);
	return (ofs & IXDP2X01_FLASH_WINDOW_MASK);
}

static struct ixp2000_flash_data ixdp2x01_flash_data = {
	.platform_data	= &ixdp2x01_flash_platform_data,
	.bank_setup	= ixdp2x01_flash_bank_setup
};

static struct resource ixdp2x01_flash_resource = {
	.start		= 0xc4000000,
	.end		= 0xc4000000 + 0x01ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp2x01_flash = {
	.name		= "IXP2000-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp2x01_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp2x01_flash_resource,
};

static struct platform_device *ixdp2x01_devices[] __initdata = {
	&ixdp2x01_flash
};

static void __init ixdp2x01_init_machine(void)
{
	*IXDP2X01_CPLD_FLASH_REG = 
		(IXDP2X01_CPLD_FLASH_BANK_MASK | IXDP2X01_CPLD_FLASH_INTERN);
	
	ixdp2x01_flash_data.nr_banks =
		((*IXDP2X01_CPLD_FLASH_REG & IXDP2X01_CPLD_FLASH_BANK_MASK) + 1);

	platform_add_devices(ixdp2x01_devices, ARRAY_SIZE(ixdp2x01_devices));
}


#ifdef CONFIG_ARCH_IXDP2401
MACHINE_START(IXDP2401, "Intel IXDP2401 Development Platform")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x00000000, IXP2000_UART_PHYS_BASE, IXP2000_UART_VIRT_BASE)
	BOOT_PARAMS(0x00000100)
	MAPIO(ixdp2x01_map_io)
	INITIRQ(ixdp2x01_init_irq)
	INITTIME(ixdp2x01_init_time)
	INIT_MACHINE(ixdp2x01_init_machine)
MACHINE_END
#endif

#ifdef CONFIG_ARCH_IXDP2801
MACHINE_START(IXDP2801, "Intel IXDP2801 Development Platform")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x00000000, IXP2000_UART_PHYS_BASE, IXP2000_UART_VIRT_BASE)
	BOOT_PARAMS(0x00000100)
	MAPIO(ixdp2x01_map_io)
	INITIRQ(ixdp2x01_init_irq)
	INITTIME(ixdp2x01_init_time)
	INIT_MACHINE(ixdp2x01_init_machine)
MACHINE_END
#endif


