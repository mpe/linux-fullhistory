/*
 * arch/arm/kernel/dec21285.c: PCI functions for DC21285
 *
 * Copyright (C) 1998-1999 Russell King, Phil Blundell
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/dec21285.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include "bios32.h"

#define MAX_SLOTS		21

extern int setup_arm_irq(int, struct irqaction *);
extern void pcibios_report_device_errors(void);

static unsigned long
dc21285_base_address(struct pci_dev *dev, int where)
{
	unsigned long addr = 0;
	unsigned int devfn = dev->devfn;

	if (dev->bus->number != 0)
		addr = PCICFG1_BASE | (dev->bus->number << 16) | (devfn << 8);
	else if (devfn < PCI_DEVFN(MAX_SLOTS, 0))
		addr = PCICFG0_BASE | 0xc00000 | (devfn << 8);

	return addr;
}

static int
dc21285_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long addr = dc21285_base_address(dev, where);
	u8 v;

	if (addr)
		asm("ldr%?b	%0, [%1, %2]"
			: "=r" (v) : "r" (addr), "r" (where));
	else
		v = 0xff;

	*value = v;

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long addr = dc21285_base_address(dev, where);
	u16 v;

	if (addr)
		asm("ldr%?h	%0, [%1, %2]"
			: "=r" (v) : "r" (addr), "r" (where));
	else
		v = 0xffff;

	*value = v;

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long addr = dc21285_base_address(dev, where);
	u32 v;

	if (addr)
		asm("ldr%?	%0, [%1, %2]"
			: "=r" (v) : "r" (addr), "r" (where));
	else
		v = 0xffffffff;

	*value = v;

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	unsigned long addr = dc21285_base_address(dev, where);

	if (addr)
		asm("str%?b	%0, [%1, %2]"
			: : "r" (value), "r" (addr), "r" (where));

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	unsigned long addr = dc21285_base_address(dev, where);

	if (addr)
		asm("str%?h	%0, [%1, %2]"
			: : "r" (value), "r" (addr), "r" (where));

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	unsigned long addr = dc21285_base_address(dev, where);

	if (addr)
		asm("str%?	%0, [%1, %2]"
			: : "r" (value), "r" (addr), "r" (where));

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dc21285_ops = {
	dc21285_read_config_byte,
	dc21285_read_config_word,
	dc21285_read_config_dword,
	dc21285_write_config_byte,
	dc21285_write_config_word,
	dc21285_write_config_dword,
};

/*
 * Warn on PCI errors.
 */
static void
dc21285_error(int irq, void *dev_id, struct pt_regs *regs)
{
	static unsigned long next_warn;
	unsigned long cmd       = *CSR_PCICMD & 0x0000ffff;
	unsigned long ctrl      = (*CSR_SA110_CNTL) & 0xffffde07;
	unsigned long irqstatus = *CSR_IRQ_RAWSTATUS;
	int warn = time_after_eq(jiffies, next_warn);

	ctrl |= SA110_CNTL_DISCARDTIMER;

	if (warn) {
		next_warn = jiffies + HZ;
		printk(KERN_DEBUG "PCI: ");
	}

	if (irqstatus & (1 << 31)) {
		if (warn)
			printk("parity error ");
		cmd |= 1 << 31;
	}

	if (irqstatus & (1 << 30)) {
		if (warn)
			printk("target abort ");
		cmd |= 1 << 28;
	}

	if (irqstatus & (1 << 29)) {
		if (warn)
			printk("master abort ");
		cmd |= 1 << 29;
	}

	if (irqstatus & (1 << 28)) {
		if (warn)
			printk("data parity error ");
		cmd |= 1 << 24;
	}

	if (irqstatus & (1 << 27)) {
		if (warn)
			printk("discard timer expired ");
		ctrl &= ~SA110_CNTL_DISCARDTIMER;
	}

	if (irqstatus & (1 << 23)) {
		if (warn)
			printk("system error ");
		ctrl |= SA110_CNTL_RXSERR;
	}

	if (warn)
		printk("pc=[<%08lX>]\n", instruction_pointer(regs));

	pcibios_report_device_errors();

	*CSR_PCICMD = cmd;
	*CSR_SA110_CNTL = ctrl;
}

static struct irqaction dc21285_error_action = {
	dc21285_error, SA_INTERRUPT, 0, "PCI error", NULL, NULL
};

void __init dc21285_init(void)
{
	static struct resource csrmem, csrio;
	unsigned int mem_size;
	unsigned long cntl;

	mem_size = (unsigned int)high_memory - PAGE_OFFSET;
	*CSR_SDRAMBASEMASK    = (mem_size - 1) & 0x0ffc0000;
	*CSR_SDRAMBASEOFFSET  = 0;
	*CSR_ROMBASEMASK      = 0x80000000;
	*CSR_CSRBASEMASK      = 0;
	*CSR_CSRBASEOFFSET    = 0;
	*CSR_PCIADDR_EXTN     = 0;

#ifdef CONFIG_HOST_FOOTBRIDGE

	csrio.flags = IORESOURCE_IO;
	csrmem.flags = IORESOURCE_MEM;

	allocate_resource(&ioport_resource, &csrio, 128,
			  0xff00, 0xffff, 128, NULL, NULL);
	allocate_resource(&iomem_resource, &csrmem, 128,
			  0xf4000000, 0xf8000000, 128, NULL, NULL);

	/*
	 * Map our SDRAM at a known address in PCI space, just in case
	 * the firmware had other ideas.  Using a nonzero base is
	 * necessary, since some VGA cards forcefully use PCI addresses
	 * in the range 0x000a0000 to 0x000c0000. (eg, S3 cards).
	 */
	*CSR_PCICACHELINESIZE = 0x00002008;
	*CSR_PCICSRBASE       = csrmem.start;
	*CSR_PCICSRIOBASE     = csrio.start;
	*CSR_PCISDRAMBASE     = virt_to_bus((void *)PAGE_OFFSET);
	*CSR_PCIROMBASE       = 0;
	*CSR_PCICMD           = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
				PCI_COMMAND_MASTER | PCI_COMMAND_FAST_BACK |
				PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY |
				(1 << 31) | (1 << 29) | (1 << 28) | (1 << 24);
#endif

	printk(KERN_DEBUG"PCI: DC21285 footbridge, revision %02lX\n",
		*CSR_CLASSREV & 0xff);

	pci_scan_bus(0, &dc21285_ops, NULL);

	/*
	 * Clear any existing errors - we aren't
	 * interested in historical data...
	 */
	cntl = *CSR_SA110_CNTL & 0xffffde07;
	*CSR_SA110_CNTL = cntl | SA110_CNTL_RXSERR;
	cntl = *CSR_PCICMD & 0x0000ffff;
	*CSR_PCICMD = cntl | 1 << 31 | 1 << 29 | 1 << 28 | 1 << 24;

	/*
	 * Initialise PCI error IRQ after we've finished probing
	 */
	setup_arm_irq(IRQ_PCI_ERR, &dc21285_error_action);
}
