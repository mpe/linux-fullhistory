/*
 * arch/arm/kernel/dec21285.c: PCI functions for DEC 21285
 *
 * Copyright (C) 1998 Russell King, Phil Blundell
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/hardware.h>

#define MAX_SLOTS		21

extern void pcibios_fixup_ebsa285(struct pci_dev *dev);
extern void pcibios_init_ebsa285(void);

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
			return PCICFG0_BASE + 0xc00000 +
				(slot << 11) + (PCI_FUNC(dev_fn) << 8);
		else
			return 0;
	} else
		return PCICFG1_BASE | (bus << 16) | (dev_fn << 8);
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
		pcibios_fixup_ebsa285(dev);

		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);

		printk(KERN_DEBUG
		       "PCI: %02x:%02x [%04x/%04x] on irq %d\n",
			dev->bus->number, dev->devfn,
			dev->vendor, dev->device, dev->irq);
	}

	hw_init();
}

__initfunc(void pcibios_init(void))
{
	unsigned int mem_size = (unsigned int)high_memory - PAGE_OFFSET;
	unsigned long cntl;

	*CSR_SDRAMBASEMASK    = (mem_size - 1) & 0x0ffc0000;
	*CSR_SDRAMBASEOFFSET  = 0;
	*CSR_ROMBASEMASK      = 0x80000000;
	*CSR_CSRBASEMASK      = 0;
	*CSR_CSRBASEOFFSET    = 0;
	*CSR_PCIADDR_EXTN     = 0;

#ifdef CONFIG_HOST_FOOTBRIDGE
	/*
	 * Against my better judgement, Philip Blundell still seems
	 * to be saying that we should initialise the PCI stuff here
	 * when the PCI_CFN bit is not set, dispite my comment below,
	 * which he decided to remove.  If it is not set, then
	 * the card is in add-in mode, and we're in a machine where
	 * the bus is set up by 'others'.
	 *
	 * We should therefore not mess about with the mapping in
	 * anyway, and we should not be using the virt_to_bus functions
	 * that exist in the HOST architecture mode (since they assume
	 * a fixed mapping).
	 *
	 * Instead, you should be using ADDIN mode, which allows for
	 * this situation.  This does assume that you have correctly
	 * initialised the PCI bus, which you must have done to get
	 * your PC booted.
	 *
	 * Unfortunately, he seems to be blind to this.  I guess he'll
	 * also remove all this.
	 *
	 * And THIS COMMENT STAYS, even if this gets patched, thank
	 * you.
	 */

	/*
	 * Map our SDRAM at a known address in PCI space, just in case
	 * the firmware had other ideas.  Using a nonzero base is
	 * necessary, since some VGA cards forcefully use PCI addresses
	 * in the range 0x000a0000 to 0x000c0000. (eg, S3 cards).
	 *
	 * NOTE! If you need to chec the PCI_CFN bit in the SA110
	 * control register then you've configured the kernel wrong.
	 * If you're not using host mode, then DO NOT set
	 * CONFIG_HOST_FOOTBRIDGE, but use CONFIG_ADDIN_FOOTBRIDGE
	 * instead.  In this case, you MUST supply some firmware
	 * to allow your PC to boot, plus we should not modify the
	 * mappings that the PC BIOS has set up for us.
	 */
	*CSR_PCICACHELINESIZE = 0x00002008;
	*CSR_PCICSRBASE       = 0;
	*CSR_PCICSRIOBASE     = 0;
	*CSR_PCISDRAMBASE     = virt_to_bus((void *)PAGE_OFFSET);
	*CSR_PCIROMBASE       = 0;
	*CSR_PCICMD           = PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
				PCI_COMMAND_MASTER | PCI_COMMAND_FAST_BACK |
				PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY |
				(1 << 31) | (1 << 29) | (1 << 28) | (1 << 24);
#endif

	/*
	 * Clear any existing errors - we aren't
	 * interested in historical data...
	 */
	cntl = *CSR_SA110_CNTL & 0xffffde07;
	*CSR_SA110_CNTL = cntl | SA110_CNTL_RXSERR;

	pcibios_init_ebsa285();

	printk(KERN_DEBUG"PCI: DEC21285 revision %02lX\n", *CSR_CLASSREV & 0xff);
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
