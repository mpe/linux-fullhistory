#define  DEBUG
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

#ifndef CONFIG_PCI

int pcibios_present(void)
{
        return 0;
}

#else /* CONFIG_PCI */

#include <linux/kernel.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/hwrpb.h>
#include <asm/io.h>


#define KB		1024
#define MB		(1024*KB)
#define GB		(1024*MB)

#define MAJOR_REV	0
#define MINOR_REV	2

/*
 * Align VAL to ALIGN, which must be a power of two.
 */
#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))


/*
 * Temporary internal macro.  If this 0, then do not write to any of
 * the PCI registers, merely read them (i.e., use configuration as
 * determined by SRM).  The SRM seem do be doing a less than perfect
 * job in configuring PCI devices, so for now we do it ourselves.
 * Reconfiguring PCI devices breaks console (RPB) callbacks, but
 * those don't work properly with 64 bit addresses anyways.
 *
 * The accepted convention seems to be that the console (POST
 * software) should fully configure boot devices and configure the
 * interrupt routing of *all* devices.  In particular, the base
 * addresses of non-boot devices need not be initialized.  For
 * example, on the AXPpci33 board, the base address a #9 GXE PCI
 * graphics card reads as zero (this may, however, be due to a bug in
 * the graphics card---there have been some rumor that the #9 BIOS
 * incorrectly resets that address to 0...).
 */
#define PCI_MODIFY		1


extern struct hwrpb_struct *hwrpb;


#if PCI_MODIFY

static unsigned int	io_base	 = 64*KB;	/* <64KB are (E)ISA ports */
static unsigned int	mem_base = 16*MB;	/* <16MB is ISA memory */


/*
 * Layout memory and I/O for a device:
 */
static void layout_dev(struct pci_dev *dev)
{
	struct pci_bus *bus;
	unsigned short cmd;
	unsigned int base, mask, size, reg;

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	for (reg = PCI_BASE_ADDRESS_0; reg <= PCI_BASE_ADDRESS_5; reg += 4) {
		/*
		 * Figure out how much space and of what type this
		 * device wants.
		 */
		pcibios_write_config_dword(bus->number, dev->devfn, reg,
					   0xffffffff);
		pcibios_read_config_dword(bus->number, dev->devfn, reg, &base);
		if (!base) {
			break;		/* done with this device */
		}
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
			base = ALIGN(io_base, size);
			io_base = base + size;
			pcibios_write_config_dword(bus->number, dev->devfn, 
						   reg, base | 0x1);
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
				/*
				 * Allocating memory below 1MB is *very*
				 * tricky, as there may be all kinds of
				 * ISA devices lurking that we don't know
				 * about.  For now, we just cross fingers
				 * and hope nobody tries to do this on an
				 * Alpha (or that the console has set it
				 * up properly).
				 */
				printk("bios32 WARNING: slot %d, function %d "
				       "requests memory below 1MB---don't "
				       "know how to do that.\n",
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
			 * aliased to the the very first 16MB of the
			 * address space (i.e., it aliases the ISA
			 * memory address space).  Thus, we try to
			 * avoid allocating PCI devices in that range.
			 * Can be allocated in 2nd-7th octant only.
			 * Devices that need more than 112MB of
			 * address space must be accessed through
			 * dense memory space only!
			 */
			base = ALIGN(mem_base, size);
			if (size > 7 * 16*MB) {
				printk("bios32 WARNING: slot %d, function %d "
				       "requests  %dB of contiguous address "
				       " space---don't use sparse memory "
				       " accesses on this device!!\n",
				       PCI_SLOT(dev->devfn),
				       PCI_FUNC(dev->devfn), size);
			} else {
				if (((base / 16*MB) & 0x7) == 0) {
					base &= ~(128*MB - 1);
					base += 16*MB;
					base  = ALIGN(base, size);
				}
				if (base / 128*MB != (base + size) / 128*MB) {
					base &= ~(128*MB - 1);
					base += (128 + 16)*MB;
					base  = ALIGN(base, size);
				}
			}
			mem_base = base + size;
			pcibios_write_config_dword(bus->number, dev->devfn,
						   reg, base);
		}
        }
	/* enable device: */
	pcibios_write_config_word(bus->number, dev->devfn, PCI_COMMAND,
				  cmd | PCI_COMMAND_MASTER);
}


static void layout_bus(struct pci_bus *bus)
{
	unsigned int l, tio, bio, tmem, bmem;
	struct pci_bus *child;
	struct pci_dev *dev;

	if (!bus->devices && !bus->children)
	  return;

	/*
	 * Align the current bases on appropriate boundaries (4K for
	 * IO and 1MB for memory).
	 */
	bio = io_base = ALIGN(io_base, 4*KB);
	bmem = mem_base = ALIGN(mem_base, 1*MB);

	/*
	 * Allocate space to each device:
	 */
	for (dev = bus->devices; dev; dev = dev->sibling) {
		if (dev->class >> 16 != PCI_BASE_CLASS_BRIDGE) {
			layout_dev(dev);
		}
	}
	/*
	 * Recursively allocate space for all of the sub-buses:
	 */
    	for (child = bus->children; child; child = child->next) {
		layout_bus(child);
        }
	/*
	 * Align the current bases on 4K and 1MB boundaries:
	 */
	tio = io_base = ALIGN(io_base, 4*KB);
	tmem = mem_base = ALIGN(mem_base, 1*MB);

	if (bus->self) {
		struct pci_dev *bridge = bus->self;
		/*
		 * Set up the top and bottom of the I/O memory segment
		 * for this bus.
		 */
		pcibios_read_config_dword(bridge->bus->number, bridge->devfn,
					  0x1c, &l);
		l = l | (bio >> 8) | ((tio - 1) & 0xf000);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x1c, l);

		l = ((bmem & 0xfff00000) >> 16) | ((tmem - 1) & 0xfff00000);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x20, l);
		/*
		 * Turn off downstream PF memory address range:
		 */
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x24, 0x0000ffff);
		/*
		 * Tell bridge that there is an ISA bus in the system:
		 */
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x3c, 0x00040000);
		/*
		 * Clear status bits, enable I/O (for downstream I/O),
		 * turn on master enable (for upstream I/O), turn on
		 * memory enable (for downstream memory), turn on
		 * master enable (for upstream memory and I/O).
		 */
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x4, 0xffff0007);
	}
}

#endif /* !PCI_MODIFY */


/*
 * Given the vendor and device ids, find the n'th instance of that device
 * in the system.  
 */
int pcibios_find_device (unsigned short vendor, unsigned short device_id,
			 unsigned short index, unsigned char *bus,
			 unsigned char *devfn)
{
        unsigned int current = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->vendor == vendor && dev->device == device_id) {
			if (current == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++current;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


/*
 * Given the class, find the n'th instance of that device
 * in the system.
 */
int pcibios_find_class (unsigned int class_code, unsigned short index,
			unsigned char *bus, unsigned char *devfn)
{
        unsigned int current = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->class == class_code) {
			if (current == index) {
				*devfn = dev->devfn;
				*bus = dev->bus->number;
				return PCIBIOS_SUCCESSFUL;
			}
			++current;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}


int pcibios_present(void)
{
        return 1;
}


unsigned long pcibios_init(unsigned long mem_start,
			   unsigned long mem_end)
{
	printk("Alpha PCI BIOS32 revision %x.%02x\n", MAJOR_REV, MINOR_REV);

#if !PCI_MODIFY
	printk("...NOT modifying existing (SRM) PCI configuration\n");
#endif
	return mem_start;
}


/*
 * Fixup configuration for Noname boards (AXPpci33).
 */
static void noname_fixup(void)
{
	struct pci_dev *dev;

	/*
	 * The Noname board has 5 PCI slots with each of the 4
	 * interrupt pins routed to different pins on the PCI/ISA
	 * bridge (PIRQ0-PIRQ3).  I don't have any information yet as
	 * to how INTB, INTC, and INTD get routed (4/12/95,
	 * davidm@cs.arizona.edu).
	 */
	static const char pirq_tab[5][4] = {
		{ 3, -1, -1, -1}, /* slot  6 (53c810) */ 
		{-1, -1, -1, -1}, /* slot  7 (PCI/ISA bridge) */
		{ 2, -1, -1, -1}, /* slot  8 (slot closest to ISA) */
		{ 1, -1, -1, -1}, /* slot  9 (middle slot) */
		{ 0, -1, -1, -1}, /* slot 10 (slot furthest from ISA) */
	};
	/*
	 * route_tab selects irq routing in PCI/ISA bridge so that:
	 *		PIRQ0 -> irq 15
	 *		PIRQ1 -> irq  9
	 *		PIRQ2 -> irq 10
	 *		PIRQ3 -> irq 11
	 */
	const unsigned int route_tab = 0x0b0a090f;
	unsigned char pin;
	int pirq;

	pcibios_write_config_dword(0, PCI_DEVFN(7, 0), 0x60, route_tab);

	/* ensure irq 9, 10, 11, and 15 are level sensitive: */
	outb((1<<(9-8)) | (1<<(10-8)) | (1<<(11-8)) | (1<<(15-8)), 0x4d1);

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */
	for (dev = pci_devices; dev; dev = dev->next) {
		dev->irq = 0;
		if (dev->bus->number != 0 ||
		    PCI_SLOT(dev->devfn) < 6 || PCI_SLOT(dev->devfn) > 10)
		{
			printk("noname_set_irq: no dev on bus %d, slot %d!!\n",
			       dev->bus->number, PCI_SLOT(dev->devfn));
			continue;
		}

		pcibios_read_config_byte(dev->bus->number, dev->devfn,
					 PCI_INTERRUPT_PIN, &pin);
		if (!pin) {
			if (dev->vendor == PCI_VENDOR_ID_S3 &&
			    (dev->device == PCI_DEVICE_ID_S3_864_1 ||
			     dev->device == PCI_DEVICE_ID_S3_864_2))
			{
				pin = 1;
			} else {
				continue;	/* no interrupt line */
			}
		}
		pirq = pirq_tab[PCI_SLOT(dev->devfn) - 6][pin - 1];
		if (pirq < 0) {
			continue;
		}
		dev->irq = (route_tab >> (8 * pirq)) & 0xff;
#if PCI_MODIFY
		/* tell the device: */
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);
#endif
	}

#if PCI_MODIFY
	{
		unsigned char hostid;
		/*
		 * SRM console version X3.9 seems to reset the SCSI
		 * host-id to 0 no matter what console environment
		 * variable pka0_host_id is set to.  Thus, if the
		 * host-id reads out as a zero, we set it to 7.  The
		 * SCSI controller is on the motherboard on bus 0,
		 * slot 6
		 */
		if (pcibios_read_config_byte(0, PCI_DEVFN(6, 0), 0x84, &hostid)
		    == PCIBIOS_SUCCESSFUL && (hostid == 0))
		{
			pcibios_write_config_byte(0, PCI_DEVFN(6, 0),
						  0x84, 7);
		}
	}
#endif /* !PCI_MODIFY */
}


unsigned long pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
#if PCI_MODIFY
	/*
	 * Scan the tree, allocating PCI memory and I/O space.
	 */
	layout_bus(&pci_root);
#endif
	
	/*
	 * Now is the time to do all those dirty little deeds...
	 */
	switch (hwrpb->sys_type) {
	      case ST_DEC_AXPPCI_33:	noname_fixup();	break;

	      default:
		printk("pcibios_fixup: don't know how to fixup sys type %ld\n",
		       hwrpb->sys_type);
		break;
	}
	return mem_start;
}

#endif /* CONFIG_PCI */
