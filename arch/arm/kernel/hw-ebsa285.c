/*
 * arch/arm/kernel/hw-ebsa286.c
 *
 * EBSA285 hardware specific functions
 *
 * Copyright (C) 1998 Russell King, Phil Blundel
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>

extern int setup_arm_irq(int, struct irqaction *);

extern void pci_set_cmd(struct pci_dev *dev, unsigned short clear, unsigned short set);
extern void pci_set_base_addr(struct pci_dev *dev, int idx, unsigned int addr);
extern void pci_set_irq_line(struct pci_dev *dev, unsigned int irq);

static int irqmap_ebsa[] __initdata = { 9, 8, 18, 11 };
static int irqmap_cats[] __initdata = { 18, 8, 9, 11 };

__initfunc(static int ebsa_irqval(struct pci_dev *dev))
{
	unsigned char pin;
	
	pcibios_read_config_byte(dev->bus->number,
				 dev->devfn,
				 PCI_INTERRUPT_PIN,
				 &pin);
	
	return irqmap_ebsa[(PCI_SLOT(dev->devfn) + pin) & 3];
}

__initfunc(static int cats_irqval(struct pci_dev *dev))
{
	if (dev->irq >= 128)
		return 32 + (dev->irq & 0x1f);

	switch (dev->irq) {
	case 1:
	case 2:
	case 3:
	case 4:
		return irqmap_cats[dev->irq - 1];
	case 0:
		return 0;
	}

	printk("PCI: device %02x:%02x has unknown irq line %x\n",
	       dev->bus->number, dev->devfn, dev->irq);
	return 0;
}

__initfunc(void pcibios_fixup_ebsa285(struct pci_dev *dev))
{
	char cmd;

	/* sort out the irq mapping for this device */
	switch (machine_type) {
	case MACH_TYPE_EBSA285:
		dev->irq = ebsa_irqval(dev);
		break;
	case MACH_TYPE_CATS:
		dev->irq = cats_irqval(dev);
		break;
	}

	/* Turn on bus mastering - boot loader doesn't
	 * - perhaps it should! - dag
	 */
	pci_read_config_byte(dev, PCI_COMMAND, &cmd);
	pci_write_config_byte(dev, PCI_COMMAND, cmd | PCI_COMMAND_MASTER);
}

static void irq_pci_err(int irq, void *dev_id, struct pt_regs *regs)
{
	const char *err = "unknown";
	unsigned long cmd = *(unsigned long *)0xfe000004 & 0xffff;
	unsigned long ctrl = *(unsigned long *)0xfe00013c & 0xffffde07;
	static unsigned long next_warn[7];
	int idx = 6;

	switch(irq) {
	case IRQ_PCIPARITY:
		*(unsigned long *)0xfe000004 = cmd | 1 << 31;
		idx = 0;
		err = "parity";
		break;

	case IRQ_PCITARGETABORT:
		*(unsigned long *)0xfe000004 = cmd | 1 << 28;
		idx = 1;
		err = "target abort";
		break;

	case IRQ_PCIMASTERABORT:
		*(unsigned long *)0xfe000004 = cmd | 1 << 29;
		idx = 2;
		err = "master abort";
		break;

	case IRQ_PCIDATAPARITY:
		*(unsigned long *)0xfe000004 = cmd | 1 << 24;
		idx = 3;
		err = "data parity";
		break;

	case IRQ_DISCARDTIMER:
		*(unsigned long *)0xfe00013c = ctrl | 1 << 8;
		idx = 4;
		err = "discard timer";
		break;

	case IRQ_SERR:
		*(unsigned long *)0xfe00013c = ctrl | 1 << 3;
		idx = 5;
		err = "system";
		break;
	}
	if (time_after_eq(jiffies, next_warn[idx])) {
		next_warn[idx] = jiffies + 3 * HZ / 100;
		printk(KERN_ERR "PCI %s error detected\n", err);
	}
}

static struct irqaction irq_pci_error = {
	irq_pci_err, SA_INTERRUPT, 0, "PCI error", NULL, NULL
};

__initfunc(void pcibios_init_ebsa285(void))
{
	setup_arm_irq(IRQ_PCIPARITY, &irq_pci_error);
	setup_arm_irq(IRQ_PCITARGETABORT, &irq_pci_error);
	setup_arm_irq(IRQ_PCIMASTERABORT, &irq_pci_error);
	setup_arm_irq(IRQ_PCIDATAPARITY, &irq_pci_error);
	setup_arm_irq(IRQ_DISCARDTIMER, &irq_pci_error);
	setup_arm_irq(IRQ_SERR, &irq_pci_error);

	/*
	 * Map our SDRAM at a known address in PCI space, just in case
	 * the firmware had other ideas.  Using a nonzero base is slightly
	 * bizarre but apparently necessary to avoid problems with some
	 * video cards.
	 *
	 * We should really only do this if the central function is enabled.
	 */
	*(unsigned long *)0xfe000010 = 0;
	*(unsigned long *)0xfe000018 = 0xe0000000;
	*(unsigned long *)0xfe0000f8 = 0;
	*(unsigned long *)0xfe0000fc = 0;
	*(unsigned long *)0xfe000100 = 0x01fc0000;
	*(unsigned long *)0xfe000104 = 0;
	*(unsigned long *)0xfe000108 = 0x80000000;
	*(unsigned long *)0xfe000004 = 0x17;
}
