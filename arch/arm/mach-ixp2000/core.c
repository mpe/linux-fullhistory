/*
 * arch/arm/mach-ixp2000/common.c
 *
 * Common routines used by all IXP2400/2800 based platforms.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (C) MontaVista Software, Inc. 
 *
 * Based on work Copyright (C) 2002-2003 Intel Corporation
 * 
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/bitops.h>
#include <linux/serial_core.h>
#include <linux/mm.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/irq.h>

static DEFINE_SPINLOCK(ixp2000_slowport_lock);
static unsigned long ixp2000_slowport_irq_flags;

/*************************************************************************
 * Slowport access routines
 *************************************************************************/
void ixp2000_acquire_slowport(struct slowport_cfg *new_cfg, struct slowport_cfg *old_cfg)
{

	spin_lock_irqsave(&ixp2000_slowport_lock, ixp2000_slowport_irq_flags);

	old_cfg->CCR = *IXP2000_SLOWPORT_CCR;
	old_cfg->WTC = *IXP2000_SLOWPORT_WTC2;
	old_cfg->RTC = *IXP2000_SLOWPORT_RTC2;
	old_cfg->PCR = *IXP2000_SLOWPORT_PCR;
	old_cfg->ADC = *IXP2000_SLOWPORT_ADC;

	ixp2000_reg_write(IXP2000_SLOWPORT_CCR, new_cfg->CCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_WTC2, new_cfg->WTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_RTC2, new_cfg->RTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_PCR, new_cfg->PCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_ADC, new_cfg->ADC);
}

void ixp2000_release_slowport(struct slowport_cfg *old_cfg)
{
	ixp2000_reg_write(IXP2000_SLOWPORT_CCR, old_cfg->CCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_WTC2, old_cfg->WTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_RTC2, old_cfg->RTC);
	ixp2000_reg_write(IXP2000_SLOWPORT_PCR, old_cfg->PCR);
	ixp2000_reg_write(IXP2000_SLOWPORT_ADC, old_cfg->ADC);

	spin_unlock_irqrestore(&ixp2000_slowport_lock, 
					ixp2000_slowport_irq_flags);
}

/*************************************************************************
 * Chip specific mappings shared by all IXP2000 systems
 *************************************************************************/
static struct map_desc ixp2000_io_desc[] __initdata = {
	{
		.virtual	= IXP2000_CAP_VIRT_BASE,
		.physical	= IXP2000_CAP_PHYS_BASE,
		.length		= IXP2000_CAP_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_INTCTL_VIRT_BASE,
		.physical	= IXP2000_INTCTL_PHYS_BASE,
		.length		= IXP2000_INTCTL_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_PCI_CREG_VIRT_BASE,
		.physical	= IXP2000_PCI_CREG_PHYS_BASE,
		.length		= IXP2000_PCI_CREG_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_PCI_CSR_VIRT_BASE,
		.physical	= IXP2000_PCI_CSR_PHYS_BASE,
		.length		= IXP2000_PCI_CSR_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_PCI_IO_VIRT_BASE,
		.physical	= IXP2000_PCI_IO_PHYS_BASE,
		.length		= IXP2000_PCI_IO_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_PCI_CFG0_VIRT_BASE,
		.physical	= IXP2000_PCI_CFG0_PHYS_BASE,
		.length		= IXP2000_PCI_CFG0_SIZE,
		.type		= MT_DEVICE
	}, {
		.virtual	= IXP2000_PCI_CFG1_VIRT_BASE,
		.physical	= IXP2000_PCI_CFG1_PHYS_BASE,
		.length		= IXP2000_PCI_CFG1_SIZE,
		.type		= MT_DEVICE
	}
};

static struct uart_port ixp2000_serial_port = {
	.membase	= (char *)(IXP2000_UART_VIRT_BASE + 3),
	.mapbase	= IXP2000_UART_PHYS_BASE + 3,
	.irq		= IRQ_IXP2000_UART,
	.flags		= UPF_SKIP_TEST,
	.iotype		= UPIO_MEM,
	.regshift	= 2,
	.uartclk	= 50000000,
	.line		= 0,
	.type		= PORT_XSCALE,
	.fifosize	= 16
};

void __init ixp2000_map_io(void)
{
	extern unsigned int processor_id;

	/*
	 * On IXP2400 CPUs we need to use MT_IXP2000_DEVICE for
	 * tweaking the PMDs so XCB=101. On IXP2800s we use the normal
	 * PMD flags.
	 */
	if ((processor_id & 0xfffffff0) == 0x69054190) {
		int i;

		printk(KERN_INFO "Enabling IXP2400 erratum #66 workaround\n");

		for(i=0;i<ARRAY_SIZE(ixp2000_io_desc);i++)
			ixp2000_io_desc[i].type = MT_IXP2000_DEVICE;
	}

	iotable_init(ixp2000_io_desc, ARRAY_SIZE(ixp2000_io_desc));
	early_serial_setup(&ixp2000_serial_port);

	/* Set slowport to 8-bit mode.  */
	ixp2000_reg_write(IXP2000_SLOWPORT_FRM, 1);
}

/*************************************************************************
 * Timer-tick functions for IXP2000
 *************************************************************************/
static unsigned ticks_per_jiffy;
static unsigned ticks_per_usec;
static unsigned next_jiffy_time;

unsigned long ixp2000_gettimeoffset (void)
{
 	unsigned long offset;

	offset = next_jiffy_time - *IXP2000_T4_CSR;

	return offset / ticks_per_usec;
}

static int ixp2000_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	/* clear timer 1 */
	ixp2000_reg_write(IXP2000_T1_CLR, 1);
	
	while ((next_jiffy_time - *IXP2000_T4_CSR) > ticks_per_jiffy) {
		timer_tick(regs);
		next_jiffy_time -= ticks_per_jiffy;
	}

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction ixp2000_timer_irq = {
	.name		= "IXP2000 Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= ixp2000_timer_interrupt
};

void __init ixp2000_init_time(unsigned long tick_rate)
{
	ixp2000_reg_write(IXP2000_T1_CLR, 0);
	ixp2000_reg_write(IXP2000_T4_CLR, 0);

	ticks_per_jiffy = (tick_rate + HZ/2) / HZ;
	ticks_per_usec = tick_rate / 1000000;

	ixp2000_reg_write(IXP2000_T1_CLD, ticks_per_jiffy - 1);
	ixp2000_reg_write(IXP2000_T1_CTL, (1 << 7));

	/*
	 * We use T4 as a monotonic counter to track missed jiffies
	 */
	ixp2000_reg_write(IXP2000_T4_CLD, -1);
	ixp2000_reg_write(IXP2000_T4_CTL, (1 << 7));
 	next_jiffy_time = 0xffffffff;

	/* register for interrupt */
	setup_irq(IRQ_IXP2000_TIMER1, &ixp2000_timer_irq);
}

/*************************************************************************
 * GPIO helpers
 *************************************************************************/
static unsigned long GPIO_IRQ_rising_edge;
static unsigned long GPIO_IRQ_falling_edge;
static unsigned long GPIO_IRQ_level_low;
static unsigned long GPIO_IRQ_level_high;

void gpio_line_config(int line, int style)
{
	unsigned long flags;

	local_irq_save(flags);

	if(style == GPIO_OUT) {
		/* if it's an output, it ain't an interrupt anymore */
		ixp2000_reg_write(IXP2000_GPIO_PDSR, (1 << line));
		GPIO_IRQ_falling_edge &= ~(1 << line);
		GPIO_IRQ_rising_edge &= ~(1 << line);
		GPIO_IRQ_level_low &= ~(1 << line);
		GPIO_IRQ_level_high &= ~(1 << line);
		ixp2000_reg_write(IXP2000_GPIO_FEDR, GPIO_IRQ_falling_edge);
		ixp2000_reg_write(IXP2000_GPIO_REDR, GPIO_IRQ_rising_edge);
		ixp2000_reg_write(IXP2000_GPIO_LSHR, GPIO_IRQ_level_high);
		ixp2000_reg_write(IXP2000_GPIO_LSLR, GPIO_IRQ_level_low);
		irq_desc[line+IRQ_IXP2000_GPIO0].valid = 0;
	} else if(style == GPIO_IN) {
		ixp2000_reg_write(IXP2000_GPIO_PDCR, (1 << line));
	}
		
	local_irq_restore(flags);
}	


/*************************************************************************
 * IRQ handling IXP2000
 *************************************************************************/
static void ixp2000_GPIO_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{                               
	int i;
	unsigned long status = *IXP2000_GPIO_INST;
		   
	for (i = 0; i <= 7; i++) {
		if (status & (1<<i)) {
			desc = irq_desc + i + IRQ_IXP2000_GPIO0;
			desc->handle(i + IRQ_IXP2000_GPIO0, desc, regs);
		}
	}
}

static void ixp2000_GPIO_irq_mask_ack(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_GPIO_INCR, (1 << (irq - IRQ_IXP2000_GPIO0)));
	ixp2000_reg_write(IXP2000_GPIO_INST, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static void ixp2000_GPIO_irq_mask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_GPIO_INCR, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static void ixp2000_GPIO_irq_unmask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_GPIO_INSR, (1 << (irq - IRQ_IXP2000_GPIO0)));
}

static struct irqchip ixp2000_GPIO_irq_chip = {
	.ack	= ixp2000_GPIO_irq_mask_ack,
	.mask	= ixp2000_GPIO_irq_mask,
	.unmask	= ixp2000_GPIO_irq_unmask
};

static void ixp2000_pci_irq_mask(unsigned int irq)
{
	unsigned long temp = *IXP2000_PCI_XSCALE_INT_ENABLE;
	if (irq == IRQ_IXP2000_PCIA)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp & ~(1 << 26)));
	else if (irq == IRQ_IXP2000_PCIB)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp & ~(1 << 27)));
}

static void ixp2000_pci_irq_unmask(unsigned int irq)
{
	unsigned long temp = *IXP2000_PCI_XSCALE_INT_ENABLE;
	if (irq == IRQ_IXP2000_PCIA)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp | (1 << 26)));
	else if (irq == IRQ_IXP2000_PCIB)
		ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, (temp | (1 << 27)));
}

static struct irqchip ixp2000_pci_irq_chip = {
	.ack	= ixp2000_pci_irq_mask,
	.mask	= ixp2000_pci_irq_mask,
	.unmask	= ixp2000_pci_irq_unmask
};

static void ixp2000_irq_mask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_CLR, (1 << irq));
}

static void ixp2000_irq_unmask(unsigned int irq)
{
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_SET,  (1 << irq));
}

static struct irqchip ixp2000_irq_chip = {
	.ack	= ixp2000_irq_mask,
	.mask	= ixp2000_irq_mask,
	.unmask	= ixp2000_irq_unmask
};

void __init ixp2000_init_irq(void)
{
	int irq;

	/*
	 * Mask all sources
	 */
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_CLR, 0xffffffff);
	ixp2000_reg_write(IXP2000_FIQ_ENABLE_CLR, 0xffffffff);

	/* clear all GPIO edge/level detects */
	ixp2000_reg_write(IXP2000_GPIO_REDR, 0);
	ixp2000_reg_write(IXP2000_GPIO_FEDR, 0);
	ixp2000_reg_write(IXP2000_GPIO_LSHR, 0);
	ixp2000_reg_write(IXP2000_GPIO_LSLR, 0);
	ixp2000_reg_write(IXP2000_GPIO_INCR, -1);

	/* clear PCI interrupt sources */
	ixp2000_reg_write(IXP2000_PCI_XSCALE_INT_ENABLE, 0);

	/*
	 * Certain bits in the IRQ status register of the 
	 * IXP2000 are reserved. Instead of trying to map
	 * things non 1:1 from bit position to IRQ number,
	 * we mark the reserved IRQs as invalid. This makes
	 * our mask/unmask code much simpler.
	 */
	for (irq = IRQ_IXP2000_SOFT_INT; irq <= IRQ_IXP2000_THDB3; irq++) {
		if((1 << irq) & IXP2000_VALID_IRQ_MASK) {
			set_irq_chip(irq, &ixp2000_irq_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, IRQF_VALID);
		} else set_irq_flags(irq, 0);
	}
	
	/*
	 * GPIO IRQs are invalid until someone sets the interrupt mode
	 * by calling gpio_line_set();
	 */
	for (irq = IRQ_IXP2000_GPIO0; irq <= IRQ_IXP2000_GPIO7; irq++) {
		set_irq_chip(irq, &ixp2000_GPIO_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, 0);
	}
	set_irq_chained_handler(IRQ_IXP2000_GPIO, ixp2000_GPIO_irq_handler);

	/*
	 * Enable PCI irqs.  The actual PCI[AB] decoding is done in
	 * entry-macro.S, so we don't need a chained handler for the
	 * PCI interrupt source.
	 */
	ixp2000_reg_write(IXP2000_IRQ_ENABLE_SET, (1 << IRQ_IXP2000_PCI));
	for (irq = IRQ_IXP2000_PCIA; irq <= IRQ_IXP2000_PCIB; irq++) {
		set_irq_chip(irq, &ixp2000_pci_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID);
	}
}

