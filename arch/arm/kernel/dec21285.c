/*
 * arch/arm/kernel/dec21285.c: PCI functions for DEC 21285
 *
 * Copyright (C) 1998 Russell King, Phil Blundell
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>

#define MAX_SLOTS		20

int
pcibios_present(void)
{
	return 1;
}

static unsigned long
pcibios_base_address(unsigned char bus, unsigned char dev_fn)
{
	if (bus == 0) {
		int slot = PCI_SLOT(dev_fn);
		
		if (slot < MAX_SLOTS)
			return 0xf8c00000 + (slot << 11);
		else
			return 0;
	} else {
		return 0xf9000000 | (bus << 16) | (dev_fn << 8);
	}
}

int
pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			 unsigned char where, unsigned char *val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);
	unsigned char v;

	if (addr)
		__asm__("ldr%?b	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
	else
		v = 0xff;
	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			 unsigned char where, unsigned short *val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);
	unsigned short v;

	if (addr)
		__asm__("ldr%?h	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
	else
		v = 0xffff;
	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			  unsigned char where, unsigned int *val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);
	unsigned int v;

	if (addr)
		__asm__("ldr%?	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
	else
		v = 0xffffffff;
	*val = v;
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			  unsigned char where, unsigned char val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);

	if (addr)
		__asm__("str%?b	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			  unsigned char where, unsigned short val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);

	if (addr)
		__asm__("str%?h	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

int
pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			   unsigned char where, unsigned int val)
{
	unsigned long addr = pcibios_base_address(bus, dev_fn);

	if (addr)
		__asm__("str%?	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

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

__initfunc(void pcibios_fixup(void))
{
	struct pci_dev *dev;
	unsigned char cmd;

	for (dev = pci_devices; dev; dev = dev->next) {
		/* sort out the irq mapping for this device */
		switch (machine_type) {
		case MACH_TYPE_EBSA285:
			dev->irq = ebsa_irqval(dev);
			break;
		case MACH_TYPE_CATS:
			dev->irq = cats_irqval(dev);
			break;
		}
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);

		printk(KERN_DEBUG
		       "PCI: %02x:%02x [%04x/%04x] on irq %d\n",
			dev->bus->number, dev->devfn,
			dev->vendor, dev->device, dev->irq);

		/* Turn on bus mastering - boot loader doesn't
		 * - perhaps it should! - dag
		 */
		pcibios_read_config_byte(dev->bus->number, dev->devfn,
					 PCI_COMMAND, &cmd);
		cmd |= PCI_COMMAND_MASTER;
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_COMMAND, cmd);
	}
}

__initfunc(void pcibios_init(void))
{
	int rev;

	rev = *(unsigned char *)0xfe000008;
	printk("DEC21285 PCI revision %02X\n", rev);

	/*
	 * Map our SDRAM at a known address in PCI space, just in case
	 * the firmware had other ideas.  Using a nonzero base is slightly
	 * bizarre but apparently necessary to avoid problems with some
	 * video cards.
	 *
	 * We should really only do this if we are the configuration master.
	 */
	*((unsigned long *)0xfe000018) = 0x10000000;
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
