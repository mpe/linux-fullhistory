/*
 * bios32.c - PCI BIOS functions for Alpha systems not using BIOS
 *	      emulation code.
 *
 * Written by Wout Klaren.
 *
 * Based on the DEC Alpha bios32.c by Dave Rusling and David Mosberger.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>

#if 0
# define DBG_DEVS(args)		printk args
#else
# define DBG_DEVS(args)
#endif

#ifdef CONFIG_PCI

/*
 * PCI support for Linux/m68k. Currently only the Hades is supported.
 *
 * Notes:
 *
 * 1. The PCI memory area starts at address 0x80000000 and the
 *    I/O area starts at 0xB0000000. Therefore these offsets
 *    are added to the base addresses when they are read and
 *    substracted when they are written.
 *
 * 2. The support for PCI bridges in the DEC Alpha version has
 *    been removed in this version.
 */

#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define KB		1024
#define MB		(1024*KB)
#define GB		(1024*MB)

#define MAJOR_REV	0
#define MINOR_REV	1

/*
 * Base addresses of the PCI memory and I/O areas on the Hades.
 */

static unsigned long pci_mem_base = 0;
static unsigned long pci_io_base = 0;

/*
 * Align VAL to ALIGN, which must be a power of two.
 */

#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))

/*
 * Calculate the address of the PCI configuration area of the given
 * device.
 *
 * BUG: boards with multiple functions are probably not correctly
 * supported.
 */

static int mk_conf_addr(unsigned char bus, unsigned char device_fn,
			unsigned char where, unsigned long *pci_addr)
{
	static const unsigned long pci_conf_base[] = { 0xA0080000, 0xA0040000,
						       0xA0020000, 0xA0010000 };
	int device = device_fn >> 3;

	DBG_DEVS(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, pci_addr=0x%p)\n",
		  bus, device_fn, where, pci_addr));

	if (device > 3) {
		DBG_DEVS(("mk_conf_addr: device (%d) > 3, returning -1\n", device));
		return -1;
	}

	*pci_addr = pci_conf_base[device] | (where);
	DBG_DEVS(("mk_conf_addr: returning pci_addr 0x%lx\n", *pci_addr));
	return 0;
}

int pcibios_read_config_byte(unsigned char bus, unsigned char device_fn,
			     unsigned char where, unsigned char *value)
{
	unsigned long pci_addr;

	*value = 0xff;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *((unsigned char *)pci_addr);

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_word(unsigned char bus, unsigned char device_fn,
			     unsigned char where, unsigned short *value)
{
	unsigned long pci_addr;

	*value = 0xffff;

	if (where & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = le16_to_cpu(*((unsigned short *)pci_addr));

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_dword(unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned int *value)
{
	unsigned long pci_addr;

	*value = 0xffffffff;

	if (where & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = le32_to_cpu(*((unsigned int *)pci_addr));

	if ((where >= PCI_BASE_ADDRESS_0) && (where <= PCI_BASE_ADDRESS_5))
	{
		if ((*value & PCI_BASE_ADDRESS_SPACE) ==
		    PCI_BASE_ADDRESS_SPACE_IO)
			*value += pci_io_base;
		else
		{
			if (*value == 0)
			{
				/*
				 * Base address is 0. Test if this base
				 * address register is used.
				 */

				*((unsigned long *)pci_addr) = 0xffffffff;
				if (*((unsigned long *)pci_addr) != 0)
					*value += pci_mem_base;
			}
			else
				*value += pci_mem_base;
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_byte(unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned char value)
{
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*((unsigned char *)pci_addr) = value;

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_word(unsigned char bus, unsigned char device_fn,
			      unsigned char where, unsigned short value)
{
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*((unsigned short *)pci_addr) = cpu_to_le16(value);

	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_dword(unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int value)
{
	unsigned long pci_addr;

	if (mk_conf_addr(bus, device_fn, where, &pci_addr) < 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if ((where >= PCI_BASE_ADDRESS_0) && (where <= PCI_BASE_ADDRESS_5))
	{
		if ((value & PCI_BASE_ADDRESS_SPACE) ==
		    PCI_BASE_ADDRESS_SPACE_IO)
			value -= pci_io_base;
		else
			value -= pci_mem_base;
	}

	*((unsigned int *)pci_addr) = cpu_to_le32(value);

	return PCIBIOS_SUCCESSFUL;
}

/*
 * Macro to enable programming of the PCI devices. On the Hades this
 * define should be true, because the Hades has no PCI BIOS.
 */

#define PCI_MODIFY		1

#if PCI_MODIFY

/*
 * Leave some room for a VGA card. We assume that the VGA card is
 * always in the first 32M of PCI memory. For the time being we do
 * not program the VGA card, because to make this work we also
 * need to change the frame buffer device.
 */

#define FIRST_IO_ADDR	0x10000
#define FIRST_MEM_ADDR	0x02000000

static unsigned int io_base = FIRST_IO_ADDR;	/* Skip first 64K. */
static unsigned int mem_base = FIRST_MEM_ADDR;	/* Skip first 32M. */

/*
 * Disable PCI device DEV so that it does not respond to I/O or memory
 * accesses.
 */

__initfunc(static void disable_dev(struct pci_dev *dev))
{
	struct pci_bus *bus;
	unsigned short cmd;

	if (dev->class >> 8 == PCI_CLASS_NOT_DEFINED_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_XGA)
		return;

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	cmd &= (~PCI_COMMAND_IO & ~PCI_COMMAND_MEMORY & ~PCI_COMMAND_MASTER);
	pcibios_write_config_word(bus->number, dev->devfn, PCI_COMMAND, cmd);
}

/*
 * Layout memory and I/O for a device:
 */

#define MAX(val1, val2) ( ((val1) > (val2)) ? val1 : val2)

__initfunc(static void layout_dev(struct pci_dev *dev, unsigned long pci_mem_base,
								  unsigned long pci_io_base))
{
	struct pci_bus *bus;
	unsigned short cmd;
	unsigned int base, mask, size, reg;
	unsigned int alignto;
	int i;

	/*
	 * Skip video cards for the time being.
	 */

	if (dev->class >> 8 == PCI_CLASS_NOT_DEFINED_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_XGA)
		return;

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	for (reg = PCI_BASE_ADDRESS_0, i = 0; reg <= PCI_BASE_ADDRESS_5; reg += 4, i++)
	{
		/*
		 * Figure out how much space and of what type this
		 * device wants.
		 */

		pcibios_write_config_dword(bus->number, dev->devfn, reg,
					   0xffffffff);
		pcibios_read_config_dword(bus->number, dev->devfn, reg, &base);

		if (!base)
		{
			/* this base-address register is unused */
			dev->base_address[i] = 0;
			continue;
		}

		/*
		 * We've read the base address register back after
		 * writing all ones and so now we must decode it.
		 */

		if (base & PCI_BASE_ADDRESS_SPACE_IO)
		{
			/*
			 * I/O space base address register.
			 */

			cmd |= PCI_COMMAND_IO;

			base &= PCI_BASE_ADDRESS_IO_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			/* align to multiple of size of minimum base */
			alignto = MAX(0x400, size) ;
			base = ALIGN(io_base, alignto);
			io_base = base + size;
			pcibios_write_config_dword(bus->number, dev->devfn,
						   reg, base | 0x1);
			dev->base_address[i] = (pci_io_base + base) | 1;
			DBG_DEVS(("layout_dev: IO address: %lX\n", base));
		}
		else
		{
			unsigned int type;

			/*
			 * Memory space base address register.
			 */

			cmd |= PCI_COMMAND_MEMORY;
			type = base & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
			base &= PCI_BASE_ADDRESS_MEM_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			switch (type)
			{
			case PCI_BASE_ADDRESS_MEM_TYPE_32:
				break;

			case PCI_BASE_ADDRESS_MEM_TYPE_64:
				printk("bios32 WARNING: "
				       "ignoring 64-bit device in "
				       "slot %d, function %d: \n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn));
				reg += 4;	/* skip extra 4 bytes */
				continue;

			case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				printk("bios32 WARNING: slot %d, function %d "
				       "requests memory below 1MB---don't "
				       "know how to do that.\n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn));
				continue;
			}

			/*
			 * Align to multiple of size of minimum base
			 */

			alignto = MAX(0x1000, size) ;
			base = ALIGN(mem_base, alignto);
			mem_base = base + size;
			pcibios_write_config_dword(bus->number, dev->devfn,
						   reg, base);
			dev->base_address[i] = pci_mem_base + base;
		}
	}

	/*
	 * Enable device:
	 */

	if (dev->class >> 8 == PCI_CLASS_NOT_DEFINED ||
	    dev->class >> 8 == PCI_CLASS_NOT_DEFINED_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_VGA ||
	    dev->class >> 8 == PCI_CLASS_DISPLAY_XGA)
	{
		/*
		 * All of these (may) have I/O scattered all around
		 * and may not use i/o-base address registers at all.
		 * So we just have to always enable I/O to these
		 * devices.
		 */
		cmd |= PCI_COMMAND_IO;
	}

	pcibios_write_config_word(bus->number, dev->devfn, PCI_COMMAND,
				  cmd | PCI_COMMAND_MASTER);
	DBG_DEVS(("layout_dev: bus %d  slot 0x%x  VID 0x%x  DID 0x%x  class 0x%x\n",
		  bus->number, PCI_SLOT(dev->devfn), dev->vendor, dev->device, dev->class));
}

__initfunc(static void layout_bus(struct pci_bus *bus, unsigned long pci_mem_base,
								  unsigned long pci_io_base))
{
	struct pci_dev *dev;

	DBG_DEVS(("layout_bus: starting bus %d\n", bus->number));

	if (!bus->devices && !bus->children)
		return;

	/*
	 * Align the current bases on appropriate boundaries (4K for
	 * IO and 1MB for memory).
	 */

	io_base = ALIGN(io_base, 4*KB);
	mem_base = ALIGN(mem_base, 1*MB);

	/*
	 * PCI devices might have been setup by a PCI BIOS emulation
	 * running under TOS. In these cases there is a
	 * window during which two devices may have an overlapping
	 * address range.  To avoid this causing trouble, we first
	 * turn off the I/O and memory address decoders for all PCI
	 * devices.  They'll be re-enabled only once all address
	 * decoders are programmed consistently.
	 */

	for (dev = bus->devices; dev; dev = dev->sibling)
	{
		if (dev->class >> 16 != PCI_BASE_CLASS_BRIDGE)
			disable_dev(dev);
	}

	/*
	 * Allocate space to each device:
	 */

	DBG_DEVS(("layout_bus: starting bus %d devices\n", bus->number));

	for (dev = bus->devices; dev; dev = dev->sibling)
	{
		if (dev->class >> 16 != PCI_BASE_CLASS_BRIDGE)
			layout_dev(dev, pci_mem_base, pci_io_base);
	}
}

#endif /* !PCI_MODIFY */

/*
 * Given the vendor and device ids, find the n'th instance of that device
 * in the system.
 */

int pcibios_find_device(unsigned short vendor, unsigned short device_id,
			unsigned short index, unsigned char *bus,
			unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next)
	{
		if (dev->vendor == vendor && dev->device == device_id)
		{
			if (curr == index)
			{
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

/*
 * Given the class, find the n'th instance of that device
 * in the system.
 */

int pcibios_find_class(unsigned int class_code, unsigned short index,
		       unsigned char *bus, unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next)
	{
		if (dev->class == class_code)
		{
			if (curr == index)
			{
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++curr;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int pcibios_present(void)
{
	if (MACH_IS_HADES)
		return 1;
	else
		return 0;
}

__initfunc(void pcibios_init(void))
{
	printk("Linux/m68k PCI BIOS32 revision %x.%02x\n", MAJOR_REV, MINOR_REV);

#if !PCI_MODIFY
	printk("...NOT modifying existing PCI configuration\n");
#endif

	pci_mem_base = 0x80000000;
	pci_io_base = 0xB0000000;
}

/*
 * static inline void hades_fixup(void)
 *
 * Assign IRQ numbers as used by Linux to the interrupt pins
 * of the PCI cards.
 */

__initfunc(static inline void hades_fixup(void))
{
	char irq_tab[4] = {
			    IRQ_TT_MFP_IO0,	/* Slot 0. */
			    IRQ_TT_MFP_IO1,	/* Slot 1. */
			    IRQ_TT_MFP_SCC,	/* Slot 2. */
			    IRQ_TT_MFP_SCSIDMA	/* Slot 3. */
			  };
	struct pci_dev *dev;
	unsigned char slot;

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */

	for (dev = pci_devices; dev; dev = dev->next)
	{
		if (dev->class >> 16 != PCI_BASE_CLASS_BRIDGE)
		{
			slot = PCI_SLOT(dev->devfn);	/* Determine slot number. */
			dev->irq = irq_tab[slot];
#if PCI_MODIFY
			pcibios_write_config_byte(dev->bus->number, dev->devfn,
						  PCI_INTERRUPT_LINE, dev->irq);
#endif
		}
	}
}

__initfunc(void pcibios_fixup(void))
{
#if PCI_MODIFY
	unsigned long orig_mem_base, orig_io_base;

	orig_mem_base = pci_mem_base;
	orig_io_base = pci_io_base;
	pci_mem_base = 0;
	pci_io_base = 0;

	/*
	 * Scan the tree, allocating PCI memory and I/O space.
	 */

	layout_bus(&pci_root, orig_mem_base, orig_io_base);

	pci_mem_base = orig_mem_base;
	pci_io_base = orig_io_base;
#endif

	/*
	 * Now is the time to do all those dirty little deeds...
	 */

	hades_fixup();
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}
#endif /* CONFIG_PCI */
