/*
 * bios32.c - PCI BIOS functions for Alpha systems not using BIOS
 *	      emulation code.
 *
 * Written by Dave Rusling (david.rusling@reo.mts.dec.com)
 *
 * Adapted to 64-bit kernel and then rewritten by David Mosberger
 * (davidm@cs.arizona.edu)
 *
 * For more information, please consult
 *
 * PCI BIOS Specification Revision
 * PCI Local Bus Specification
 * PCI System Design Guide
 *
 * PCI Special Interest Group
 * M/S HF3-15A
 * 5200 N.E. Elam Young Parkway
 * Hillsboro, Oregon 97124-6497
 * +1 (503) 696-2000
 * +1 (800) 433-5177
 *
 * Manuals are $25 each or $50 for all three, plus $7 shipping
 * within the United States, $35 abroad.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/tasks.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <asm/pci.h>
#include <asm/dma.h>

#include "proto.h"
#include "bios32.h"

#define DEBUG_DEVS 0
#define DEBUG_HOSE 0

#if DEBUG_DEVS
# define DBG_DEVS(args)		printk args
#else
# define DBG_DEVS(args)
#endif

#if DEBUG_HOSE
# define DBG_HOSE(args)		printk args
#else
# define DBG_HOSE(args)
#endif

#ifndef CONFIG_PCI

asmlinkage int sys_pciconfig_read() { return -ENOSYS; }
asmlinkage int sys_pciconfig_write() { return -ENOSYS; }
void reset_for_srm(void) { }

#else /* CONFIG_PCI */

#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/segment.h>
#include <asm/system.h>

/*
 * PCI public interfaces.
 */

#define MAJOR_REV	0
#define MINOR_REV	4	/* minor revision 4, add multi-PCI handling */

struct linux_hose_info *bus2hose[256];
struct linux_hose_info *hose_head, **hose_tail = &hose_head;
int hose_count;
int pci_probe_enabled;

static void layout_hoses(void);

int
pcibios_present(void)
{
	return alpha_mv.hose_read_config_byte != NULL;
}

void __init
pcibios_init(void)
{
	if (!pcibios_present())
		return;

	printk("Alpha PCI BIOS32 revision %d.%02d\n", MAJOR_REV, MINOR_REV);
	if (alpha_use_srm_setup)
		printk("   NOT modifying existing (SRM) PCI configuration\n");
}

char * __init
pcibios_setup(char *str)
{
	return str;
}

void __init
pcibios_fixup(void)
{
	alpha_mv.pci_fixup();
}

void __init
pcibios_fixup_bus(struct pci_bus *bus)
{
}

int
pcibios_read_config_byte (u8 bus, u8 dev, u8 where, u8 *value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	*value = 0xff;
	if (alpha_mv.hose_read_config_byte) {
		r = (alpha_mv.hose_read_config_byte
		     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

int
pcibios_read_config_word (u8 bus, u8 dev, u8 where, u16 *value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	*value = 0xffff;
	if (alpha_mv.hose_read_config_word) {
		r = PCIBIOS_BAD_REGISTER_NUMBER;
		if (!(where & 1))
			r = (alpha_mv.hose_read_config_word
			     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

int
pcibios_read_config_dword (u8 bus, u8 dev, u8 where, u32 *value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	*value = 0xffffffff;
	if (alpha_mv.hose_read_config_dword) {
		r = PCIBIOS_BAD_REGISTER_NUMBER;
		if (!(where & 3))
			r = (alpha_mv.hose_read_config_dword
			     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

int
pcibios_write_config_byte (u8 bus, u8 dev, u8 where, u8 value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	if (alpha_mv.hose_write_config_byte) {
		r = (alpha_mv.hose_write_config_byte
		     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

int
pcibios_write_config_word (u8 bus, u8 dev, u8 where, u16 value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	if (alpha_mv.hose_write_config_word) {
		r = PCIBIOS_BAD_REGISTER_NUMBER;
		if (!(where & 1))
			r = (alpha_mv.hose_write_config_word
			     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

int
pcibios_write_config_dword (u8 bus, u8 dev, u8 where, u32 value)
{
	int r = PCIBIOS_FUNC_NOT_SUPPORTED;
	if (alpha_mv.hose_write_config_dword) {
		r = PCIBIOS_BAD_REGISTER_NUMBER;
		if (!(where & 3))
			r = (alpha_mv.hose_write_config_dword
			     (bus, dev, where, value, bus2hose[bus]));
	}
	return r;
}

asmlinkage int
sys_pciconfig_read(unsigned long bus, unsigned long dfn,
		   unsigned long off, unsigned long len,
		   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;
	
	switch (len) {
	case 1:
		err = pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, buf);
		break;
	case 2:
		err = pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		err = pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, (unsigned int *)buf);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

asmlinkage int
sys_pciconfig_write(unsigned long bus, unsigned long dfn,
		    unsigned long off, unsigned long len,
		    unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!pcibios_present())
		return -ENOSYS;

	switch (len) {
	case 1:
		err = get_user(ubyte, buf);
		if (err)
			break;
		err = pcibios_write_config_byte(bus, dfn, off, ubyte);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if (err)
			break;
		err = pcibios_write_config_word(bus, dfn, off, ushort);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if (err)
			break;
		err = pcibios_write_config_dword(bus, dfn, off, uint);
		if (err != PCIBIOS_SUCCESSFUL) {
			err = -EFAULT;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}


/*
 * Gory details start here...
 */

/*
 * Align VAL to ALIGN, which must be a power of two.
 */
#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))


/* 
 * The following structure records initial configuration of devices
 * so that we can reset them on shutdown and so enable clean reboots
 * on SRM.  It is more trouble than it iw worth to conditionalize this.
 */

struct srm_irq_reset {
	struct srm_irq_reset *next;
	struct pci_dev *dev;
	u8 irq;
} *srm_irq_resets;

struct srm_io_reset {
	struct srm_io_reset *next;
	struct pci_dev *dev;
	u32 io;
	u8 reg;
} *srm_io_resets;

/* Apply the collected reset modifications.  */

void
reset_for_srm(void)
{
	struct srm_irq_reset *qreset;
	struct srm_io_reset *ireset;

	/* Reset any IRQs that we changed.  */
	for (qreset = srm_irq_resets; qreset ; qreset = qreset->next) {
		pcibios_write_config_byte(qreset->dev->bus->number,
					  qreset->dev->devfn,
					  PCI_INTERRUPT_LINE,
					  qreset->irq);
#if 1
		printk("reset_for_srm: bus %d slot 0x%x "
		       "SRM IRQ 0x%x changed back from 0x%x\n",
		       qreset->dev->bus->number,
		       PCI_SLOT(qreset->dev->devfn),
		       qreset->irq, qreset->dev->irq);
#endif
	}

	/* Reset any IO addresses that we changed.  */
	for (ireset = srm_io_resets; ireset ; ireset = ireset->next) {
		pcibios_write_config_dword(ireset->dev->bus->number,
					   ireset->dev->devfn,
					   ireset->reg, ireset->io);
#if 1
		printk("reset_for_srm: bus %d slot 0x%x "
		       "SRM MEM/IO restored to 0x%x\n",
		       ireset->dev->bus->number,
		       PCI_SLOT(ireset->dev->devfn),
		       ireset->io);
#endif
	}
}

static void
new_irq_reset(struct pci_dev *dev, u8 irq)
{
	struct srm_irq_reset *n;
	n = kmalloc(sizeof(*n), GFP_KERNEL);

	n->next = srm_irq_resets;
	n->dev = dev;
	n->irq = irq;
	srm_irq_resets = n;
}

static void
new_io_reset(struct pci_dev *dev, u8 reg, u32 io)
{
	struct srm_io_reset *n;
	n = kmalloc(sizeof(*n), GFP_KERNEL);

	n->next = srm_io_resets;
	n->dev = dev;
	n->reg = reg;
	n->io = io;
	srm_io_resets = n;
}


/*
 * Disable PCI device DEV so that it does not respond to I/O or memory
 * accesses.
 */
static void __init
disable_dev(struct pci_dev *dev)
{
	struct pci_bus *bus;
	unsigned short cmd;

	/*
	 * HACK: the PCI-to-EISA bridge does not seem to identify
	 *       itself as a bridge... :-(
	 */
	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82375) {
		dev->class = PCI_CLASS_BRIDGE_EISA;
		DBG_DEVS(("disable_dev: ignoring PCEB...\n"));
		return;
	}

	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82378) {
		dev->class = PCI_CLASS_BRIDGE_ISA;
		DBG_DEVS(("disable_dev: ignoring SIO...\n"));
		return;
	}

	/*
	 * We don't have code that will init the CYPRESS bridge correctly
	 * so we do the next best thing, and depend on the previous
	 * console code to do the right thing, and ignore it here... :-\
	 */
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    dev->device == PCI_DEVICE_ID_CONTAQ_82C693) {
		DBG_DEVS(("disable_dev: ignoring CYPRESS bridge...\n"));
		return;
	}

#if DEBUG_DEVS && 0
	/* Worse HACK: Don't disable the video card, so I can see where
	   it is *really* falling over.  */
	if (dev->class >> 16 == PCI_BASE_CLASS_DISPLAY) {
		DBG_DEVS(("disable_dev: ignoring video card %04x:%04x\n",
			  dev->vendor, dev->device));
		return;
	}
#endif

	DBG_DEVS(("disable_dev: disabling %04x:%04x\n",
		  dev->vendor, dev->device));

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	/* hack, turn it off first... */
	cmd &= (~PCI_COMMAND_IO & ~PCI_COMMAND_MEMORY & ~PCI_COMMAND_MASTER);
	pcibios_write_config_word(bus->number, dev->devfn, PCI_COMMAND, cmd);
}


/*
 * Layout memory and I/O for a device:
 */
#define MAX(val1, val2) ((val1) > (val2) ? (val1) : (val2))

static unsigned int io_base;
static unsigned int mem_base; 

static void __init
layout_dev(struct pci_dev *dev)
{
	struct pci_bus *bus;
	unsigned short cmd;
	unsigned int base, mask, size, off, idx;
	unsigned int orig_base;
	unsigned int alignto;
	unsigned long handle;

	/*
	 * HACK: the PCI-to-EISA bridge does not seem to identify
	 *       itself as a bridge... :-(
	 */
	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82375) {
		dev->class = PCI_CLASS_BRIDGE_EISA;
		DBG_DEVS(("layout_dev: ignoring PCEB...\n"));
		return;
	}

	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82378) {
		dev->class = PCI_CLASS_BRIDGE_ISA;
		DBG_DEVS(("layout_dev: ignoring SIO...\n"));
		return;
	}

	/*
	 * We don't have code that will init the CYPRESS bridge correctly
	 * so we do the next best thing, and depend on the previous
	 * console code to do the right thing, and ignore it here... :-\
	 */
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    dev->device == PCI_DEVICE_ID_CONTAQ_82C693) {
		DBG_DEVS(("layout_dev: ignoring CYPRESS bridge...\n"));
		return;
	}

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	for (idx = 0; idx <= 5; idx++) {
		off = PCI_BASE_ADDRESS_0 + 4*idx;
		/*
		 * Figure out how much space and of what type this
		 * device wants.
		 */
		pcibios_read_config_dword(bus->number, dev->devfn, off,
					  &orig_base);
		pcibios_write_config_dword(bus->number, dev->devfn, off,
					   0xffffffff);
		pcibios_read_config_dword(bus->number, dev->devfn, off, &base);
		if (!base) {
			/* this base-address register is unused */
			dev->base_address[idx] = 0;
			continue;
		}

		DBG_DEVS(("layout_dev: slot %d fn %d off 0x%x base 0x%x\n",
			  PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
			  off, base));

		/*
		 * We've read the base address register back after
		 * writing all ones and so now we must decode it.
		 */
		if (base & PCI_BASE_ADDRESS_SPACE_IO) {
			/*
			 * I/O space base address register.
			 */
			cmd |= PCI_COMMAND_IO;

			base &= PCI_BASE_ADDRESS_IO_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			/*
			 * Aligning to 0x800 rather than the minimum base of
			 * 0x400 is an attempt to avoid having devices in 
			 * any 0x?C?? range, which is where the de4x5 driver
			 * probes for EISA cards.
			 *
			 * Adaptecs, especially, resent such intrusions.
			 */
			alignto = MAX(0x800, size);
			base = ALIGN(io_base, alignto);
			io_base = base + size;

			pcibios_write_config_dword(bus->number, dev->devfn, 
						   off, base | 0x1);
			new_io_reset(dev, off, orig_base);

			handle = PCI_HANDLE(bus->number) | base | 1;
			dev->base_address[idx] = handle;

			DBG_DEVS(("layout_dev: dev 0x%x IO @ 0x%lx (0x%x)\n",
				  dev->device, handle, size));
		} else {
			unsigned int type;
			/*
			 * Memory space base address register.
			 */
			cmd |= PCI_COMMAND_MEMORY;
			type = base & PCI_BASE_ADDRESS_MEM_TYPE_MASK;
			base &= PCI_BASE_ADDRESS_MEM_MASK;
			mask = (~base << 1) | 0x1;
			size = (mask & base) & 0xffffffff;
			switch (type) {
			case PCI_BASE_ADDRESS_MEM_TYPE_32:
			case PCI_BASE_ADDRESS_MEM_TYPE_64:
				break;

			case PCI_BASE_ADDRESS_MEM_TYPE_1M:
				/*
				 * Allocating memory below 1MB is *very*
				 * tricky, as there may be all kinds of
				 * ISA devices lurking that we don't know
				 * about.  For now, we just cross fingers
				 * and hope nobody tries to do this on an
				 * Alpha (or that the console has set it
				 * up properly).
				 */
				printk("bios32 WARNING: slot %d, function %d"
				       " requests memory below 1MB---don't"
				       " know how to do that.\n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn));
				continue;
			}
			/*
			 * The following holds at least for the Low Cost
			 * Alpha implementation of the PCI interface:
			 *
			 * In sparse memory address space, the first
			 * octant (16MB) of every 128MB segment is
			 * aliased to the very first 16 MB of the
			 * address space (i.e., it aliases the ISA
			 * memory address space).  Thus, we try to
			 * avoid allocating PCI devices in that range.
			 * Can be allocated in 2nd-7th octant only.
			 * Devices that need more than 112MB of
			 * address space must be accessed through
			 * dense memory space only!
			 */
			/* align to multiple of size of minimum base */
			alignto = MAX(0x1000, size);
			base = ALIGN(mem_base, alignto);
			if (size > 7 * 16*MB) {
				printk("bios32 WARNING: slot %d, function %d"
				       " requests 0x%x bytes of contiguous"
				       " address space---don't use sparse"
				       " memory accesses on this device!!\n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn), size);
			} else {
				if (((base / (16*MB)) & 0x7) == 0) {
					base &= ~(128*MB - 1);
					base += 16*MB;
					base  = ALIGN(base, alignto);
				}
				if (base/(128*MB) != (base + size)/(128*MB)) {
					base &= ~(128*MB - 1);
					base += (128 + 16)*MB;
					base  = ALIGN(base, alignto);
				}
			}
			mem_base = base + size;

			pcibios_write_config_dword(bus->number, dev->devfn,
						   off, base);
			new_io_reset(dev, off, orig_base);

			handle = PCI_HANDLE(bus->number) | base;
			dev->base_address[idx] = handle;

			/*
			 * Currently for 64-bit cards, we simply do the usual
			 * for setup of the first register (low) of the pair,
			 * and then clear out the second (high) register, as
			 * we are not yet able to do 64-bit addresses, and
			 * setting the high register to 0 allows 32-bit SAC
			 * addresses to be used.
			 */
			if (type == PCI_BASE_ADDRESS_MEM_TYPE_64) {
				unsigned int orig_base2;
				pcibios_read_config_dword(bus->number,
							  dev->devfn,
							  off+4, &orig_base2);
				if (0 != orig_base2) {
					pcibios_write_config_dword(bus->number,
								   dev->devfn,
								   off+4, 0);
					new_io_reset (dev, off+4, orig_base2);
				}
				/* Bypass hi reg in the loop.  */
				dev->base_address[++idx] = 0;

				printk("bios32 WARNING: "
				       "handling 64-bit device in "
				       "slot %d, function %d: \n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn));
			}

			DBG_DEVS(("layout_dev: dev 0x%x MEM @ 0x%lx (0x%x)\n",
				  dev->device, handle, size));
		}
	}

	/* Enable device: */
	if (dev->class >> 8 == PCI_CLASS_NOT_DEFINED ||
	    dev->class >> 8 == PCI_CLASS_NOT_DEFINED_VGA ||
	    dev->class >> 8 == PCI_CLASS_STORAGE_IDE ||
	    dev->class >> 16 == PCI_BASE_CLASS_DISPLAY)
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

	DBG_DEVS(("layout_dev: bus %d slot %d VID 0x%x DID 0x%x"
		  " class 0x%x cmd 0 x%x\n",
		  bus->number, PCI_SLOT(dev->devfn), dev->vendor,
		  dev->device, dev->class, cmd|PCI_COMMAND_MASTER));
}

static int __init
layout_bus(struct pci_bus *bus)
{
	unsigned int l, tio, bio, tmem, bmem;
	struct pci_bus *child;
	struct pci_dev *dev;
	int found_vga = 0;

	DBG_DEVS(("layout_bus: starting bus %d\n", bus->number));

	if (!bus->devices && !bus->children)
		return 0;

	/*
	 * Align the current bases on appropriate boundaries (4K for
	 * IO and 1MB for memory).
	 */
	bio = io_base = ALIGN(io_base, 4*KB);
	bmem = mem_base = ALIGN(mem_base, 1*MB);

	/*
	 * There are times when the PCI devices have already been
	 * setup (e.g., by MILO or SRM).  In these cases there is a
	 * window during which two devices may have an overlapping
	 * address range.  To avoid this causing trouble, we first
	 * turn off the I/O and memory address decoders for all PCI
	 * devices.  They'll be re-enabled only once all address
	 * decoders are programmed consistently.
	 */
	DBG_DEVS(("layout_bus: disable_dev for bus %d\n", bus->number));

	for (dev = bus->devices; dev; dev = dev->sibling) {
		if ((dev->class >> 16 != PCI_BASE_CLASS_BRIDGE) ||
		    (dev->class >> 8 == PCI_CLASS_BRIDGE_PCMCIA)) {
			disable_dev(dev);
		}
	}

	/*
	 * Allocate space to each device:
	 */
	DBG_DEVS(("layout_bus: starting bus %d devices\n", bus->number));

	for (dev = bus->devices; dev; dev = dev->sibling) {
		if ((dev->class >> 16 != PCI_BASE_CLASS_BRIDGE) ||
		    (dev->class >> 8 == PCI_CLASS_BRIDGE_PCMCIA)) {
			layout_dev(dev);
		}
		if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
			found_vga = 1;
	}
	/*
	 * Recursively allocate space for all of the sub-buses:
	 */
	DBG_DEVS(("layout_bus: starting bus %d children\n", bus->number));

    	for (child = bus->children; child; child = child->next) {
		found_vga += layout_bus(child);
	}
	/*
	 * Align the current bases on 4K and 1MB boundaries:
	 */
	tio = io_base = ALIGN(io_base, 4*KB);
	tmem = mem_base = ALIGN(mem_base, 1*MB);

	if (bus->self) {
		struct pci_dev *bridge = bus->self;

		DBG_DEVS(("layout_bus: config bus %d bridge\n", bus->number));

		/*
		 * Set up the top and bottom of the PCI I/O segment
		 * for this bus.
		 */
		pcibios_read_config_dword(bridge->bus->number, bridge->devfn,
					  PCI_IO_BASE, &l);
		l &= 0xffff0000;
		l |= ((bio >> 8) & 0x00f0) | ((tio - 1) & 0xf000);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_IO_BASE, l);

		/*
		 * Clear out the upper 16 bits of IO base/limit.
		 * Clear out the upper 32 bits of PREF base/limit.
		 */
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_IO_BASE_UPPER16, 0);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_PREF_BASE_UPPER32, 0);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_PREF_LIMIT_UPPER32, 0);

		/*
		 * Set up the top and bottom of the PCI Memory segment
		 * for this bus.
		 */
		l = ((bmem & 0xfff00000) >> 16) | ((tmem - 1) & 0xfff00000);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_MEMORY_BASE, l);

		/*
		 * Turn off downstream PF memory address range, unless
		 * there is a VGA behind this bridge, in which case, we
		 * enable the PREFETCH range to include BIOS ROM at C0000.
		 *
		 * NOTE: this is a bit of a hack, done with PREFETCH for
		 * simplicity, rather than having to add it into the above
		 * non-PREFETCH range, which could then be bigger than we want.
		 * We might assume that we could relocate the BIOS ROM, but
		 * that would depend on having it found by those who need it
		 * (the DEC BIOS emulator would find it, but I do not know
		 * about the Xservers). So, we do it this way for now... ;-}
		 */
		l = (found_vga) ? 0 : 0x0000ffff;
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_PREF_MEMORY_BASE, l);

		/*
		 * Tell bridge that there is an ISA bus in the system,
		 * and (possibly) a VGA as well.
		 */
		l = (found_vga) ? 0x0c : 0x04;
		pcibios_write_config_byte(bridge->bus->number, bridge->devfn,
					   PCI_BRIDGE_CONTROL, l);

		/*
		 * Clear status bits,
		 * turn on I/O    enable (for downstream I/O),
		 * turn on memory enable (for downstream memory),
		 * turn on master enable (for upstream memory and I/O).
		 */
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   PCI_COMMAND, 0xffff0007);
	}
	DBG_DEVS(("layout_bus: bus %d finished\n", bus->number));
	return found_vga;
}

void __init
layout_all_busses(unsigned long default_io_base,
		  unsigned long default_mem_base)
{
	struct pci_bus *cur;

	layout_hoses();

	/*
	 * Scan the tree, allocating PCI memory and I/O space.
	 */
	/*
	 * Sigh; check_region() will need changing to accept a PCI_HANDLE,
	 * if we allocate I/O space addresses on a per-bus basis.
	 * For now, make the I/O bases unique across all busses, so
	 * that check_region() will not get confused... ;-}
	 */
	io_base = default_io_base;
	for (cur = &pci_root; cur; cur = cur->next) {
		mem_base = default_mem_base;
		DBG_DEVS(("layout_all_busses: calling layout_bus()\n"));
		layout_bus(cur);
	}
	DBG_DEVS(("layout_all_busses: done.\n"));
}


/*
 * The SRM console *disables* the IDE interface, this code ensures it's
 * enabled.
 *
 * This code bangs on a control register of the 87312 Super I/O chip
 * that implements parallel port/serial ports/IDE/FDI.  Depending on
 * the motherboard, the Super I/O chip can be configured through a
 * pair of registers that are located either at I/O ports 0x26e/0x26f
 * or 0x398/0x399.  Unfortunately, autodetecting which base address is
 * in use works only once (right after a reset).  The Super I/O chip
 * has the additional quirk that configuration register data must be
 * written twice (I believe this is a safety feature to prevent
 * accidental modification---fun, isn't it?).
 */

void __init
enable_ide(long ide_base)
{
	int data;
	unsigned long flags;

	__save_and_cli(flags);
	outb(0, ide_base);		/* set the index register for reg #0 */
	data = inb(ide_base+1);		/* read the current contents */
	outb(0, ide_base);		/* set the index register for reg #0 */
	outb(data | 0x40, ide_base+1);	/* turn on IDE */
	outb(data | 0x40, ide_base+1);	/* turn on IDE, really! */
	__restore_flags(flags);
}

/* Look for mis-configured devices' I/O space addresses behind bridges.  */
static void
check_behind_io(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	unsigned int reg, orig_base, new_base, found_one = 0;

	for (reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5; reg += 4) {
		/* Read the current setting, check for I/O space and >= 64K */
		pcibios_read_config_dword(bus->number, dev->devfn,
					  reg, &orig_base);

		if (!orig_base || !(orig_base & PCI_BASE_ADDRESS_SPACE_IO))
			continue; /* unused or non-IO */

		if (orig_base < 64*1024) {
#if 1
printk("check_behind_io: ALREADY OK! bus %d slot %d base 0x%x\n",
       bus->number, PCI_SLOT(dev->devfn), orig_base);
#endif
			if (orig_base & ~1)
				continue; /* OK! */
			orig_base = 0x12001; /* HACK! FIXME!! */
		}

		/* HACK ALERT! for now, just subtract 32K from the
		   original address, which should give us addresses
		   in the range 0x8000 and up */
		new_base = orig_base - 0x8000;
#if 1
printk("check_behind_io: ALERT! bus %d slot %d old 0x%x new 0x%x\n",
       bus->number, PCI_SLOT(dev->devfn), orig_base, new_base);
#endif
		pcibios_write_config_dword(bus->number, dev->devfn,
					   reg, new_base);

		new_io_reset(dev, reg, orig_base);
		found_one++;
	}

	/* If any were modified, gotta hack the bridge IO limits too. */
	if (found_one) {
		if (bus->self) {
			struct pci_dev *bridge = bus->self;
			unsigned int l;
			/*
			 * Set up the top and bottom of the PCI I/O segment
			 * for this bus.
			 */
			pcibios_read_config_dword(bridge->bus->number,
						  bridge->devfn, 0x1c, &l);
#if 1
printk("check_behind_io: ALERT! bus %d slot %d oldLIM 0x%x\n",
       bus->number, PCI_SLOT(bridge->devfn), l);
#endif
			l = (l & 0xffff0000U) | 0xf080U; /* give it ALL */
			pcibios_write_config_dword(bridge->bus->number,
						   bridge->devfn, 0x1c, l);
			pcibios_write_config_dword(bridge->bus->number,
						   bridge->devfn,
						   0x3c, 0x00040000);
			pcibios_write_config_dword(bridge->bus->number,
						   bridge->devfn,
						   0x4, 0xffff0007);
		} else
			printk("check_behind_io: WARNING! bus->self NULL\n");
	}
}


/*
 * Most boards share most of the fixup code, which is isolated here.
 */

void __init
common_pci_fixup(int (*map_irq)(struct pci_dev *dev, int slot, int pin),
		 int (*swizzle)(struct pci_dev *dev, int *pin))
{
	struct pci_dev *dev;
	u8 pin, slot, irq_orig;
	int irq;

	/*
	 * Go through all devices, fixing up irqs as we see fit.
	 */
	for (dev = pci_devices; dev; dev = dev->next) {
		if ((dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) &&
		    (dev->class >> 8 != PCI_CLASS_BRIDGE_PCMCIA))
			continue;

		/*
		 * We don't have code that will init the CYPRESS bridge
		 * correctly so we do the next best thing, and depend on
		 * the previous console code to do the right thing, and
		 * ignore it here... :-\
		 */
		if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
		    dev->device == PCI_DEVICE_ID_CONTAQ_82C693) {
			DBG_DEVS(("common_pci_fixup: ignoring CYPRESS bridge...\n"));
			continue;
		}

		/*
		 * This device is not on the primary bus, we need
		 * to figure out which interrupt pin it will come
		 * in on.   We know which slot it will come in on
		 * 'cos that slot is where the bridge is.   Each
		 * time the interrupt line passes through a PCI-PCI
		 * bridge we must apply the swizzle function (see
		 * the inline static routine above).
		 */
		dev->irq = 0;

		pcibios_read_config_byte(dev->bus->number, dev->devfn,
					 PCI_INTERRUPT_PIN, &pin);
		/* Cope with 0 and illegal. */
		if (pin == 0 || pin > 4)
			pin = 1;

		if (!DEV_IS_ON_PRIMARY(dev)) {
			/* Follow the chain of bridges, swizzling as we go.  */

			int spill = pin;
			slot = (*swizzle)(dev, &spill);
			pin = spill;

			/* Must make sure that SRM didn't screw up
			   and allocate an address > 64K for I/O
			   space behind a PCI-PCI bridge. */
			if (alpha_use_srm_setup)
				check_behind_io(dev);
		} else {
			/* Just a device on a primary bus.  */
			slot = PCI_SLOT(dev->devfn);
		}

		irq = (*map_irq)(dev, slot, pin);

		DBG_DEVS(("common_pci_fixup: bus %d slot %d "
			  "pin %d irq %d\n",
			  dev->bus->number, slot, pin, irq));

		if (irq != -1)
			dev->irq = irq;

		if (alpha_using_srm) {
			/* Read the original SRM-set IRQ and tell. */
			pcibios_read_config_byte(dev->bus->number,
						 dev->devfn,
						 PCI_INTERRUPT_LINE,
						 &irq_orig);

			if (irq_orig != dev->irq) {
				DBG_DEVS(("common_pci_fixup: bus %d "
					  "slot 0x%x SRM IRQ 0x%x "
					  "changed to 0x%x\n",
					  dev->bus->number,
					  PCI_SLOT(dev->devfn),
					  irq_orig, dev->irq));

				new_irq_reset(dev, irq_orig);
			}
		}

		/* Always tell the device, so the driver knows what is
		   the real IRQ to use; the device does not use it. */
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);

		DBG_DEVS(("common_pci_fixup: bus %d slot 0x%x"
			  " VID 0x%x DID 0x%x\n"
			  "              int_slot 0x%x pin 0x%x"
			  " pirq 0x%x\n",
			  dev->bus->number, PCI_SLOT(dev->devfn),
			  dev->vendor, dev->device,
			  slot, pin, dev->irq));

		/*
		 * If it's a VGA, enable its BIOS ROM at C0000.
		 */
		if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
			/* But if its a Cirrus 543x/544x DISABLE it,
			   since enabling ROM disables the memory... */
			if ((dev->vendor == PCI_VENDOR_ID_CIRRUS) &&
			    (dev->device >= 0x00a0) &&
			    (dev->device <= 0x00ac)) {
				pcibios_write_config_dword(
					dev->bus->number,
					dev->devfn,
					PCI_ROM_ADDRESS,
					0x00000000);
			} else {
				pcibios_write_config_dword(
					dev->bus->number,
					dev->devfn,
					PCI_ROM_ADDRESS,
					0x000c0000 | PCI_ROM_ADDRESS_ENABLE);
			}
		}
		/*
		 * If it's a SCSI, disable its BIOS ROM.
		 */
		if ((dev->class >> 8) == PCI_CLASS_STORAGE_SCSI) {
			pcibios_write_config_dword(dev->bus->number,
						   dev->devfn,
						   PCI_ROM_ADDRESS,
						   0x0000000);
		}
	}
}

/* Most Alphas have straight-forward swizzling needs.  */

int __init
common_swizzle(struct pci_dev *dev, int *pinp)
{
	int pin = *pinp;
	do {
		pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		/* Move up the chain of bridges. */
		dev = dev->bus->self;
	} while (dev->bus->self);
	*pinp = pin;

	/* The slot is the slot of the last bridge. */
	return PCI_SLOT(dev->devfn);
}

/*
 * On multiple bus machines, in order to cope with a somewhat deficient
 * API, we must map the 8-bit bus identifier so that it is unique across
 * multiple interfaces (hoses).  At the same time we do this, chain the
 * other hoses off of pci_root so that they will be found during normal
 * PCI probing and layout.
 */

#define PRIMARY(b)	((b)&0xff)
#define SECONDARY(b)	(((b)>>8)&0xff)
#define SUBORDINATE(b)	(((b)>>16)&0xff)

static int __init
hose_scan_bridges(struct linux_hose_info *hose, unsigned char bus)
{
	unsigned int devfn, l, class;
	unsigned char hdr_type = 0;
	unsigned int found = 0;

	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			alpha_mv.hose_read_config_byte(bus, devfn,
						       PCI_HEADER_TYPE,
						       &hdr_type, hose);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		/* Check if there is anything here. */
		alpha_mv.hose_read_config_dword(bus, devfn, PCI_VENDOR_ID,
						&l, hose);
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		/* See if this is a bridge device. */
		alpha_mv.hose_read_config_dword(bus, devfn, PCI_CLASS_REVISION,
						&class, hose);

		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int busses;

			found++;

			alpha_mv.hose_read_config_dword(bus, devfn,
							PCI_PRIMARY_BUS,
							&busses, hose);

			DBG_HOSE(("hose_scan_bridges: hose %d bus %d "
				  "slot %d busses 0x%x\n",
				  hose->pci_hose_index, bus, PCI_SLOT(devfn),
				  busses));

			/*
			 * Do something with first_busno and last_busno
			 */
			if (hose->pci_first_busno > PRIMARY(busses)) {
				hose->pci_first_busno = PRIMARY(busses);
				DBG_HOSE(("hose_scan_bridges: hose %d bus %d "
					  "slot %d change first to %d\n",
					  hose->pci_hose_index, bus,
					 PCI_SLOT(devfn), PRIMARY(busses)));
			}
			if (hose->pci_last_busno < SUBORDINATE(busses)) {
				hose->pci_last_busno = SUBORDINATE(busses);
				DBG_HOSE(("hose_scan_bridges: hose %d bus %d "
					  "slot %d change last to %d\n",
					  hose->pci_hose_index, bus,
					  PCI_SLOT(devfn),
					  SUBORDINATE(busses)));
			}
			/*
			 * Now scan everything underneath the bridge.
			 */
			hose_scan_bridges(hose, SECONDARY(busses));
		}
	}
	return found;
}

static void __init
hose_reconfigure_bridges(struct linux_hose_info *hose, unsigned char bus)
{
	unsigned int devfn, l, class;
	unsigned char hdr_type = 0;

	for (devfn = 0; devfn < 0xff; ++devfn) {
		if (PCI_FUNC(devfn) == 0) {
			alpha_mv.hose_read_config_byte(bus, devfn,
						       PCI_HEADER_TYPE,
						       &hdr_type, hose);
		} else if (!(hdr_type & 0x80)) {
			/* not a multi-function device */
			continue;
		}

		/* Check if there is anything here. */
		alpha_mv.hose_read_config_dword(bus, devfn, PCI_VENDOR_ID,
						&l, hose);
		if (l == 0xffffffff || l == 0x00000000) {
			hdr_type = 0;
			continue;
		}

		/* See if this is a bridge device. */
		alpha_mv.hose_read_config_dword(bus, devfn, PCI_CLASS_REVISION,
						&class, hose);

		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			unsigned int busses;

			alpha_mv.hose_read_config_dword(bus, devfn,
							PCI_PRIMARY_BUS,
							&busses, hose);

			/*
			 * First reconfigure everything underneath the bridge.
			 */
			hose_reconfigure_bridges(hose, (busses >> 8) & 0xff);

			/*
			 * Unconfigure this bridges bus numbers,
			 * pci_scan_bus() will fix this up properly.
			 */
			busses &= 0xff000000;
			alpha_mv.hose_write_config_dword(bus, devfn,
							 PCI_PRIMARY_BUS,
							 busses, hose);
		}
	}
}

static void __init
hose_fixup_busno(struct linux_hose_info *hose, unsigned char bus)
{
	int nbus;

	/*
	 * First, scan for all bridge devices underneath this hose,
	 * to determine the first and last busnos.
	 */
	DBG_HOSE(("hose_fixup_busno: before hose_scan_bridges()\n"));

	if (!hose_scan_bridges(hose, 0)) {
		/* none found, exit */
		hose->pci_first_busno = bus;
		hose->pci_last_busno = bus;
	} else {
		/*
		 * Reconfigure all bridge devices underneath this hose.
		 */
		DBG_HOSE(("hose_fixup_busno: before hose_reconfigure_bridges\n"));
		hose_reconfigure_bridges(hose, hose->pci_first_busno);
	}

	/*
	 * Now reconfigure the hose to it's new bus number and set up
	 * our bus2hose mapping for this hose.
	 */
	nbus = hose->pci_last_busno - hose->pci_first_busno;

	hose->pci_first_busno = bus;

	DBG_HOSE(("hose_fixup_busno: hose %d startbus %d nbus %d\n",
		  hose->pci_hose_index, bus, nbus));

	do {
		bus2hose[bus++] = hose;
	} while (nbus-- > 0);
	DBG_HOSE(("hose_fixup_busno: returning...\n"));
}

static void __init
layout_one_hose(struct linux_hose_info *hose)
{
	static struct pci_bus *pchain = NULL;
	struct pci_bus *pbus = &hose->pci_bus;
	static unsigned char busno = 0;

	DBG_HOSE(("layout_one_hose: entry\n"));

	/*
	 * Hoses include child PCI bridges in bus-range property,
	 * but we don't scan each of those ourselves, Linux generic PCI
	 * probing code will find child bridges and link them into this
	 * hose's root PCI device hierarchy.
	 */

	pbus->number = pbus->secondary = busno;
	pbus->sysdata = hose;

	DBG_HOSE(("layout_one_hose: before hose_fixup_busno()\n"));

	hose_fixup_busno(hose, busno);

	DBG_HOSE(("layout_one_hose: before pci_scan_bus()\n"));

	pbus->subordinate = pci_scan_bus(pbus); /* the original! */

	/*
	 * Set the maximum subordinate bus of this hose.
	 */
	hose->pci_last_busno = pbus->subordinate;
#if 0
	alpha_mv.hose_write_config_byte(busno, 0, 0x41, hose->pci_last_busno,
					hose);
#endif
	busno = pbus->subordinate + 1;

	/*
	 * Fixup the chain of primary PCI busses.
	 */
	if (pchain) {
		pchain->next = &hose->pci_bus;
		pchain = pchain->next;
	} else {
		pchain = &pci_root;
		memcpy(pchain, &hose->pci_bus, sizeof(pci_root));
	}
	DBG_HOSE(("layout_one_hose: returning...\n"));
}

static void __init
layout_hoses(void)
{
	struct linux_hose_info * hose;
	int i;

	/* On multiple bus machines, we play games with pci_root in order
	   that all of the busses are probed as part of the normal PCI
	   setup.  The existance of the busses was determined in init_arch.  */

	if (hose_head) {
		/* Multi-bus machines did not yet wish to allow bus
		   accesses.  We now do our own thing after the normal
		   pci_scan_bus is over.  This mechanism is relatively
		   broken but will be fixed later.  */
		pci_probe_enabled = 1;

		for (hose = hose_head; hose; hose = hose->next)
			layout_one_hose(hose);
	} else {
		/* For the benefit of single-bus machines, emulate a
		   multi-bus machine to the (limited) extent necessary. 
		   Init all bus2hose entries to point to a dummy.  */
		hose = kmalloc(sizeof(*hose), GFP_KERNEL);
		memset(hose, 0, sizeof(*hose));
		for (i = 0; i < 256; ++i)
			bus2hose[i] = hose;
	}
}

#endif /* CONFIG_PCI */
