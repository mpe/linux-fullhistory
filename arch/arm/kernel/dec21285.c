/*
 * arch/arm/kernel/dec21285.c: PCI functions for DEC 21285
 *
 * Copyright (C) 1998 Russell King, Phil Blundell
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>

#define MAX_SLOTS		20

extern void pcibios_fixup_ebsa285(struct pci_dev *dev);
extern void pcibios_init_ebsa285(void);
extern void pcibios_fixup_vnc(struct pci_dev *dev);
extern void pcibios_init_vnc(void);

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
			return 0xf8c00000 + (slot << 11) + (PCI_FUNC(dev_fn) << 8);
		else
			return 0;
	} else
		return 0xf9000000 | (bus << 16) | (dev_fn << 8);
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

__initfunc(void pci_set_cmd(struct pci_dev *dev, unsigned short clear, unsigned short set))
{
	unsigned short cmd;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd = (cmd & ~clear) | set;
	pci_write_config_word(dev, PCI_COMMAND, cmd);
}

__initfunc(void pci_set_base_addr(struct pci_dev *dev, int idx, unsigned int addr))
{
	int reg = PCI_BASE_ADDRESS_0 + (idx << 2);

	pci_write_config_dword(dev, reg, addr);
	pci_read_config_dword(dev, reg, &addr);

	dev->base_address[idx] = addr;
}

__initfunc(void pcibios_fixup(void))
{
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (machine_is_ebsa285() || machine_is_cats())
			pcibios_fixup_ebsa285(dev);
		if (machine_is_netwinder())
			pcibios_fixup_vnc(dev);

		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);

		printk(KERN_DEBUG
		       "PCI: %02x:%02x [%04x/%04x] on irq %d\n",
			dev->bus->number, dev->devfn,
			dev->vendor, dev->device, dev->irq);
	}
	if (machine_is_netwinder())
		hw_init();
}

__initfunc(void pcibios_init(void))
{
	if (machine_is_ebsa285() || machine_is_cats())
		pcibios_init_ebsa285();
	if (machine_is_netwinder())
		pcibios_init_vnc();

	printk("DEC21285 PCI revision %02X\n", *(unsigned char *)0xfe000008);
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
