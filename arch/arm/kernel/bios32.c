/*
 * arch/arm/kernel/bios32.c: PCI functions for ARM
 *
 * Copyright (C) 1998 Russell King
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

int pcibios_present(void)
{
	return 1;
}

static unsigned long pcibios_base_address(unsigned char dev_fn)
{
	int slot = PCI_SLOT(dev_fn);

	if (slot < 4)
		return 0xf8000000 + (1 << (19 - slot));
	else
		return 0;
}

int pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char where, unsigned char *val)
{
	unsigned long addr = pcibios_base_address(dev_fn);
	unsigned char v;

	if (addr) {
		__asm__("ldr%?b	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
		*val = v;
	} else
		*val = 0xff;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char where, unsigned short *val)
{
	unsigned long addr = pcibios_base_address(dev_fn);
	unsigned short v;

	if (addr) {
		__asm__("ldrh%?	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
		*val = v;
	} else
		*val = 0xffff;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char where, unsigned int *val)
{
	unsigned long addr = pcibios_base_address(dev_fn);
	unsigned int v;

	if (addr) {
		__asm__("ldr%?	%0, [%1, %2]"
			: "=r" (v)
			: "r" (addr), "r" (where));
		*val = v;
	} else
		*val = 0xffffffff;
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
			     unsigned char where, unsigned char val)
{
	unsigned long addr = pcibios_base_address(dev_fn);

	if (addr)
		__asm__("str%?b	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char where, unsigned short val)
{
	unsigned long addr = pcibios_base_address(dev_fn);

	if (addr)
		__asm__("strh%?	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char where, unsigned int val)
{
	unsigned long addr = pcibios_base_address(dev_fn);

	if (addr)
		__asm__("str%?	%0, [%1, %2]"
			: : "r" (val), "r" (addr), "r" (where));
	return PCIBIOS_SUCCESSFUL;
}

static int irq[] = { 18, 8, 9, 11 };

__initfunc(void pcibios_fixup(void))
{
	struct pci_dev *dev;
	unsigned char pin;

	for (dev = pci_devices; dev; dev = dev->next) {
		pcibios_read_config_byte(dev->bus->number,
					 dev->devfn,
					 PCI_INTERRUPT_PIN,
					 &pin);

		dev->irq = irq[(PCI_SLOT(dev->devfn) + pin) & 3];

		pcibios_write_config_byte(dev->bus->number,
					  dev->devfn,
					  PCI_INTERRUPT_LINE,
					  dev->irq);

		printk("PCI: %02x:%02x [%04x/%04x] pin %d irq %d\n",
			dev->bus->number, dev->devfn,
			dev->vendor, dev->device,
			pin, dev->irq);
	}
}

__initfunc(void pcibios_init(void))
{
	int rev;

	rev = *(unsigned char *)0xfe000008;
	printk("DEC21285 PCI revision %02X\n", rev);
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
