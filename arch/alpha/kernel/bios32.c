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
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <asm/dma.h>

#if 0
# define DBG_DEVS(args)		printk args
#else
# define DBG_DEVS(args)
#endif

#ifndef CONFIG_PCI

int pcibios_present(void)
{
	return 0;
}

asmlinkage int sys_pciconfig_read()
{
	return -ENOSYS;
}

asmlinkage int sys_pciconfig_write()
{
	return -ENOSYS;
}

#else /* CONFIG_PCI */

#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/malloc.h>
#include <linux/mm.h>

#include <asm/hwrpb.h>
#include <asm/io.h>
#include <asm/uaccess.h>


#define KB		1024
#define MB		(1024*KB)
#define GB		(1024*MB)

#define MAJOR_REV	0
#define MINOR_REV	3

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

/* Forward declarations for some extra fixup routines for specific hardware. */
#if defined(CONFIG_ALPHA_PC164) || defined(CONFIG_ALPHA_LX164)
extern int SMC93x_Init(void);
#endif
#ifdef CONFIG_ALPHA_SX164
extern int SMC669_Init(void);
#endif
#ifdef CONFIG_ALPHA_MIATA
static int es1888_init(void);
#endif

#if PCI_MODIFY

/*
 * NOTE: we can't just blindly use 64K for machines with EISA busses; they
 * may also have PCI-PCI bridges present, and then we'd configure the bridge
 * incorrectly
 *
 * Also, we start at 0x8000 or 0x9000, in hopes to get all devices'
 * IO space areas allocated *before* 0xC000; this is because certain
 * BIOSes (Millennium for one) use PCI Config space "mechanism #2"
 * accesses to probe the bus. If a device's registers appear at 0xC000,
 * it may see an INx/OUTx at that address during BIOS emulation of the
 * VGA BIOS, and some cards, notably Adaptec 2940UW, take mortal offense.
 */
#if defined(CONFIG_ALPHA_EISA)
static unsigned int    io_base  = 0x9000;      /* start above 8th slot */
#else
static unsigned int    io_base  = 0x8000;
#endif

#if defined(CONFIG_ALPHA_XL)
/*
 * An XL is AVANTI (APECS) family, *but* it has only 27 bits of ISA address
 * that get passed through the PCI<->ISA bridge chip. Although this causes
 * us to set the PCI->Mem window bases lower than normal, we still allocate
 * PCI bus devices' memory addresses *below* the low DMA mapping window,
 * and hope they fit below 64Mb (to avoid conflicts), and so that they can
 * be accessed via SPARSE space.
 *
 * We accept the risk that a broken Myrinet card will be put into a true XL
 * and thus can more easily run into the problem described below.
 */
static unsigned int mem_base = 16*MB + 2*MB; /* 16M to 64M-1 is avail */

#elif defined(CONFIG_ALPHA_LCA) || defined(CONFIG_ALPHA_APECS)
/*
 * We try to make this address *always* have more than 1 bit set.
 * this is so that devices like the broken Myrinet card will always have
 * a PCI memory address that will never match a IDSEL address in
 * PCI Config space, which can cause problems with early rev cards.
 *
 * However, APECS and LCA have only 34 bits for physical addresses, thus
 * limiting PCI bus memory addresses for SPARSE access to be less than 128Mb.
 */
static unsigned int mem_base = 64*MB + 2*MB;

#else
/*
 * We try to make this address *always* have more than 1 bit set.
 * this is so that devices like the broken Myrinet card will always have
 * a PCI memory address that will never match a IDSEL address in
 * PCI Config space, which can cause problems with early rev cards.
 *
 * Because CIA and PYXIS and T2 have more bits for physical addresses,
 * they support an expanded range of SPARSE memory addresses.
 */
static unsigned int	mem_base = 128*MB + 16*MB;

#endif

/*
 * Disable PCI device DEV so that it does not respond to I/O or memory
 * accesses.
 */
static void disable_dev(struct pci_dev *dev)
{
	struct pci_bus *bus;
	unsigned short cmd;

#ifdef CONFIG_ALPHA_EISA
	/*
	 * HACK: the PCI-to-EISA bridge does not seem to identify
	 *       itself as a bridge... :-(
	 */
	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82375) {
		DBG_DEVS(("disable_dev: ignoring PCEB...\n"));
		return;
	}
#endif
#ifdef CONFIG_ALPHA_SX164
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    /* FIXME: We want a symbolic device name here.  */
	    dev->device == 0xc693) {
		DBG_DEVS(("disable_dev: ignoring CYPRESS bridge...\n"));
		return;
	}
#endif

	bus = dev->bus;
	pcibios_read_config_word(bus->number, dev->devfn, PCI_COMMAND, &cmd);

	/* hack, turn it off first... */
	cmd &= (~PCI_COMMAND_IO & ~PCI_COMMAND_MEMORY & ~PCI_COMMAND_MASTER);
	pcibios_write_config_word(bus->number, dev->devfn, PCI_COMMAND, cmd);
}


/*
 * Layout memory and I/O for a device:
 */
#define MAX(val1, val2) ((val1) > (val2) ? val1 : val2)

static void layout_dev(struct pci_dev *dev)
{
	struct pci_bus *bus;
	unsigned short cmd;
	unsigned int base, mask, size, reg;
	unsigned int alignto;

#ifdef CONFIG_ALPHA_EISA
	/*
	 * HACK: the PCI-to-EISA bridge does not seem to identify
	 *       itself as a bridge... :-(
	 */
	if (dev->vendor == PCI_VENDOR_ID_INTEL &&
	    dev->device == PCI_DEVICE_ID_INTEL_82375) {
		DBG_DEVS(("layout_dev: ignoring PCEB...\n"));
		return;
	}
#endif
#ifdef CONFIG_ALPHA_SX164
	if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
	    dev->device == 0xc693) {
		DBG_DEVS(("layout_dev: ignoring CYPRESS bridge...\n"));
		return;
	}
#endif

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
			/* this base-address register is unused */
			dev->base_address[(reg - PCI_BASE_ADDRESS_0)>>2] = 0;
			continue;
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
						   reg, base | 0x1);
			dev->base_address[(reg - PCI_BASE_ADDRESS_0)>>2]
				= base | 0x1;
			DBG_DEVS(("layout_dev: dev 0x%x IO @ 0x%x (0x%x)\n",
				  dev->device, base, size));
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
			 * aliased to the the very first 16MB of the
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
						   reg, base);
			dev->base_address[(reg-PCI_BASE_ADDRESS_0)>>2] = base;
			DBG_DEVS(("layout_dev: dev 0x%x MEM @ 0x%x (0x%x)\n",
				  dev->device, base, size));
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


static void layout_bus(struct pci_bus *bus)
{
	unsigned int l, tio, bio, tmem, bmem;
	struct pci_bus *child;
	struct pci_dev *dev;

	DBG_DEVS(("layout_bus: starting bus %d\n", bus->number));

	if (!bus->devices && !bus->children)
		return;

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
	}
	/*
	 * Recursively allocate space for all of the sub-buses:
	 */
	DBG_DEVS(("layout_bus: starting bus %d children\n", bus->number));

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
		 * Set up the top and bottom of the PCI I/O segment
		 * for this bus.
		 */
		pcibios_read_config_dword(bridge->bus->number, bridge->devfn,
					  0x1c, &l);
		l &= 0xffff0000;
		l |= ((bio >> 8) & 0x00f0) | ((tio - 1) & 0xf000);
		pcibios_write_config_dword(bridge->bus->number, bridge->devfn,
					   0x1c, l);
		/*
		 * Set up the top and bottom of the  PCI Memory segment
		 * for this bus.
		 */
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
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->vendor == vendor && dev->device == device_id) {
			if (curr == index) {
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
int pcibios_find_class (unsigned int class_code, unsigned short index,
			unsigned char *bus, unsigned char *devfn)
{
	unsigned int curr = 0;
	struct pci_dev *dev;

	for (dev = pci_devices; dev; dev = dev->next) {
		if (dev->class == class_code) {
			if (curr == index) {
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
static inline void enable_ide(long ide_base)
{
	int data;

	outb(0, ide_base);		/* set the index register for reg #0 */
	data = inb(ide_base+1);		/* read the current contents */
	outb(0, ide_base);		/* set the index register for reg #0 */
	outb(data | 0x40, ide_base+1);	/* turn on IDE */
	outb(data | 0x40, ide_base+1);	/* turn on IDE, really! */
}

/* 
 * A small note about bridges and interrupts.    The DECchip 21050 (and later)
 * adheres to the PCI-PCI bridge specification.   This says that the
 * interrupts on the other side of a bridge are swizzled in the following
 * manner:
 *
 * Dev    Interrupt   Interrupt 
 *        Pin on      Pin on 
 *        Device      Connector
 *
 *   4    A           A
 *        B           B
 *        C           C
 *        D           D
 * 
 *   5    A           B
 *        B           C
 *        C           D
 *        D           A
 *
 *   6    A           C
 *        B           D
 *        C           A
 *        D           B
 *
 *   7    A           D
 *        B           A
 *        C           B
 *        D           C
 *
 *   Where A = pin 1, B = pin 2 and so on and pin=0 = default = A.
 *   Thus, each swizzle is ((pin-1) + (device#-4)) % 4
 *
 *   The following code is somewhat simplistic as it assumes only one bridge.
 *   I will fix it later (david.rusling@reo.mts.dec.com).
 */
static inline unsigned char
bridge_swizzle(unsigned char pin, unsigned int slot) 
{
	/* swizzle */
	return (((pin-1) + slot) % 4) + 1;
}

/*
 * Most evaluation boards share most of the fixup code, which is isolated
 * here.  This function is declared "inline" as only one platform will ever
 * be selected in any given kernel.  If that platform doesn't need this code,
 * we don't want it around as dead code.
 */
static inline void
common_fixup(long min_idsel, long max_idsel, long irqs_per_slot,
	     char irq_tab[max_idsel - min_idsel + 1][irqs_per_slot],
	     long ide_base)
{
	struct pci_dev *dev;
	unsigned char pin;
	unsigned char slot;

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */
	for (dev = pci_devices; dev; dev = dev->next) {
		if ((dev->class >> 16 != PCI_BASE_CLASS_BRIDGE
		     /* PCEB (PCI to EISA bridge) does not identify
		        itself as a bridge... :-P */
		     && !(dev->vendor == PCI_VENDOR_ID_INTEL &&
			  dev->device == PCI_DEVICE_ID_INTEL_82375))
                    || dev->class >> 8 == PCI_CLASS_BRIDGE_PCMCIA) {
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
			if (dev->bus->number != 0) {
				struct pci_dev *curr = dev;
				/* read the pin and do the PCI-PCI bridge
				   interrupt pin swizzle */
				pcibios_read_config_byte(dev->bus->number,
							 dev->devfn,
							 PCI_INTERRUPT_PIN,
							 &pin);
				/* cope with 0 */
				if (pin == 0)
					pin = 1;
				/* follow the chain of bridges, swizzling
				   as we go */
#if defined(CONFIG_ALPHA_MIATA)
				slot = PCI_SLOT(dev->devfn) + 5;
				DBG_DEVS(("MIATA: bus 1 slot %d pin %d"
					  " irq %d min_idsel %d\n",
					  PCI_SLOT(dev->devfn), pin,
					  irq_tab[slot - min_idsel][pin],
					  min_idsel));
#elif defined(CONFIG_ALPHA_NORITAKE)
				/* WAG Alert! */
				slot = PCI_SLOT(dev->devfn) + 14;
				DBG_DEVS(("NORITAKE: bus 1 slot %d pin %d"
					  " irq %d min_idsel %d\n",
					  PCI_SLOT(dev->devfn), pin,
					  irq_tab[slot - min_idsel][pin],
					  min_idsel));
#else
				do {
					/* swizzle */
					pin = bridge_swizzle(pin, PCI_SLOT(curr->devfn));
					/* move up the chain of bridges */
					curr = curr->bus->self;
				} while (curr->bus->self);
				/* The slot is the slot of the last bridge. */
				slot = PCI_SLOT(curr->devfn);
#endif /* MIATA */
			} else {
				/* work out the slot */
				slot = PCI_SLOT(dev->devfn);
				/* read the pin */
				pcibios_read_config_byte(dev->bus->number,
							 dev->devfn,
							 PCI_INTERRUPT_PIN,
							 &pin);
			}
			if (irq_tab[slot - min_idsel][pin] != -1)
				dev->irq = irq_tab[slot - min_idsel][pin];
#if PCI_MODIFY
			/* tell the device: */
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn,
						  PCI_INTERRUPT_LINE,
						  dev->irq);
#endif

			DBG_DEVS(("common_fixup: bus %d slot 0x%x"
				  " VID 0x%x DID 0x%x\n"
				  "              int_slot 0x%x pin 0x%x"
				  " pirq 0x%x\n",
				  dev->bus->number, PCI_SLOT(dev->devfn),
				  dev->vendor, dev->device,
				  slot, pin, dev->irq));

			/*
			 * if it's a VGA, enable its BIOS ROM at C0000
			 */
			if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
				pcibios_write_config_dword(dev->bus->number,
							   dev->devfn,
							   PCI_ROM_ADDRESS,
							   0x000c0000 | PCI_ROM_ADDRESS_ENABLE);
			}
			/*
			 * if it's a SCSI, disable its BIOS ROM
			 */
			if ((dev->class >> 8) == PCI_CLASS_STORAGE_SCSI) {
				pcibios_write_config_dword(dev->bus->number,
							   dev->devfn,
							   PCI_ROM_ADDRESS,
							   0x0000000);
			}
		}
#ifdef CONFIG_ALPHA_SX164
		/* If it the CYPRESS PCI-ISA bridge, disable IDE
		   interrupt routing through PCI (ie do through PIC).  */
		else if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
			 dev->device == 0xc693 &&
			 PCI_FUNC(dev->devfn) == 0) {
			pcibios_write_config_word(dev->bus->number,
						  dev->devfn, 0x04, 0x0007);

			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x40, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x41, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x42, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x43, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x44, 0x27);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x45, 0xe0);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x48, 0xf0);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x49, 0x40);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x4a, 0x00);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x4b, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x4c, 0x80);
			pcibios_write_config_byte(dev->bus->number,
						  dev->devfn, 0x4d, 0x70);

			outb(0, DMA1_RESET_REG);
			outb(0, DMA2_RESET_REG);
			outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
			outb(0, DMA2_MASK_REG);
		}
#endif /* SX164 */
	}
	if (ide_base) {
		enable_ide(ide_base);
	}
}

/*
 * The EB66+ is very similar to the EB66 except that it does not have
 * the on-board NCR and Tulip chips.  In the code below, I have used
 * slot number to refer to the id select line and *not* the slot
 * number used in the EB66+ documentation.  However, in the table,
 * I've given the slot number, the id select line and the Jxx number
 * that's printed on the board.  The interrupt pins from the PCI slots
 * are wired into 3 interrupt summary registers at 0x804, 0x805 and
 * 0x806 ISA.
 *
 * In the table, -1 means don't assign an IRQ number.  This is usually
 * because it is the Saturn IO (SIO) PCI/ISA Bridge Chip.
 */
static inline void eb66p_fixup(void)
{
	static char irq_tab[5][5] __initlocaldata = {
		{16+0, 16+0, 16+5,  16+9, 16+13},  /* IdSel 6,  slot 0, J25 */
		{16+1, 16+1, 16+6, 16+10, 16+14},  /* IdSel 7,  slot 1, J26 */
		{  -1,   -1,   -1,    -1,    -1},  /* IdSel 8,  SIO         */
		{16+2, 16+2, 16+7, 16+11, 16+15},  /* IdSel 9,  slot 2, J27 */
		{16+3, 16+3, 16+8, 16+12,  16+6}   /* IdSel 10, slot 3, J28 */
	};
	common_fixup(6, 10, 5, irq_tab, 0x398);
}


/*
 * The PC164/LX164 has 19 PCI interrupts, four from each of the four PCI
 * slots, the SIO, PCI/IDE, and USB.
 * 
 * Each of the interrupts can be individually masked. This is
 * accomplished by setting the appropriate bit in the mask register.
 * A bit is set by writing a "1" to the desired position in the mask
 * register and cleared by writing a "0". There are 3 mask registers
 * located at ISA address 804h, 805h and 806h.
 * 
 * An I/O read at ISA address 804h, 805h, 806h will return the
 * state of the 11 PCI interrupts and not the state of the MASKED
 * interrupts.
 * 
 * Note: A write to I/O 804h, 805h, and 806h the mask register will be
 * updated.
 * 
 * 
 * 				ISA DATA<7:0>
 * ISA     +--------------------------------------------------------------+
 * ADDRESS |   7   |   6   |   5   |   4   |   3   |   2  |   1   |   0   |
 *         +==============================================================+
 * 0x804   | INTB0 |  USB  |  IDE  |  SIO  | INTA3 |INTA2 | INTA1 | INTA0 |
 *         +--------------------------------------------------------------+
 * 0x805   | INTD0 | INTC3 | INTC2 | INTC1 | INTC0 |INTB3 | INTB2 | INTB1 |
 *         +--------------------------------------------------------------+
 * 0x806   | Rsrv  | Rsrv  | Rsrv  | Rsrv  | Rsrv  |INTD3 | INTD2 | INTD1 |
 *         +--------------------------------------------------------------+
 *         * Rsrv = reserved bits
 *         Note: The mask register is write-only.
 * 
 * IdSel	
 *   5	 32 bit PCI option slot 2
 *   6	 64 bit PCI option slot 0
 *   7	 64 bit PCI option slot 1
 *   8	 Saturn I/O
 *   9	 32 bit PCI option slot 3
 *  10	 USB
 *  11	 IDE
 * 
 */

#if defined(CONFIG_ALPHA_PC164) || defined(CONFIG_ALPHA_LX164)
static inline void alphapc164_fixup(void)
{
	static char irq_tab[7][5] __initlocaldata = {
		/*INT   INTA  INTB   INTC   INTD */
		{ 16+2, 16+2, 16+9,  16+13, 16+17}, /* IdSel  5, slot 2, J20 */
		{ 16+0, 16+0, 16+7,  16+11, 16+15}, /* IdSel  6, slot 0, J29 */
		{ 16+1, 16+1, 16+8,  16+12, 16+16}, /* IdSel  7, slot 1, J26 */
		{   -1,   -1,   -1,    -1,    -1},  /* IdSel  8, SIO */
		{ 16+3, 16+3, 16+10, 16+14, 16+18}, /* IdSel  9, slot 3, J19 */
		{ 16+6, 16+6, 16+6,  16+6,  16+6},  /* IdSel 10, USB */
		{ 16+5, 16+5, 16+5,  16+5,  16+5}   /* IdSel 11, IDE */
	};

	common_fixup(5, 11, 5, irq_tab, 0);
	SMC93x_Init();
}
#endif

/*
 * The AlphaPC64 is very similar to the EB66+ except that its slots
 * are numbered differently.  In the code below, I have used slot
 * number to refer to the id select line and *not* the slot number
 * used in the AlphaPC64 documentation.  However, in the table, I've
 * given the slot number, the id select line and the Jxx number that's
 * printed on the board.  The interrupt pins from the PCI slots are
 * wired into 3 interrupt summary registers at 0x804, 0x805 and 0x806
 * ISA.
 *
 * In the table, -1 means don't assign an IRQ number.  This is usually
 * because it is the Saturn IO (SIO) PCI/ISA Bridge Chip.
 */
static inline void cabriolet_fixup(void)
{
	static char irq_tab[5][5] __initlocaldata = {
		{ 16+2, 16+2, 16+7, 16+11, 16+15}, /* IdSel 5,  slot 2, J21 */
		{ 16+0, 16+0, 16+5,  16+9, 16+13}, /* IdSel 6,  slot 0, J19 */
		{ 16+1, 16+1, 16+6, 16+10, 16+14}, /* IdSel 7,  slot 1, J20 */
		{   -1,   -1,   -1,    -1,    -1}, /* IdSel 8,  SIO         */
		{ 16+3, 16+3, 16+8, 16+12, 16+16}  /* IdSel 9,  slot 3, J22 */
	};

	common_fixup(5, 9, 5, irq_tab, 0x398);
}


/*
 * Fixup configuration for EB66/EB64+ boards.
 *
 * Both these boards use the same interrupt summary scheme.  There are
 * two 8 bit external summary registers as follows:
 *
 * Summary @ 0x26:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 0
 * 1        Interrupt Line A from slot 1
 * 2        Interrupt Line B from slot 0
 * 3        Interrupt Line B from slot 1
 * 4        Interrupt Line C from slot 0
 * 5        Interrupt line from the two ISA PICs
 * 6        Tulip (slot 
 * 7        NCR SCSI
 *
 * Summary @ 0x27
 * Bit      Meaning
 * 0        Interrupt Line C from slot 1
 * 1        Interrupt Line D from slot 0
 * 2        Interrupt Line D from slot 1
 * 3        RAZ
 * 4        RAZ
 * 5        RAZ
 * 6        RAZ
 * 7        RAZ
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  5       NCR SCSI controller
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       Intel SIO PCI-ISA bridge chip
 *  9       Tulip - DECchip 21040 ethernet controller
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
static inline void eb66_and_eb64p_fixup(void)
{
	static char irq_tab[5][5] __initlocaldata = {
		{16+7, 16+7, 16+7, 16+7,  16+7},  /* IdSel 5,  slot ?, ?? */
		{16+0, 16+0, 16+2, 16+4,  16+9},  /* IdSel 6,  slot ?, ?? */
		{16+1, 16+1, 16+3, 16+8, 16+10},  /* IdSel 7,  slot ?, ?? */
		{  -1,   -1,   -1,   -1,    -1},  /* IdSel 8,  SIO */
		{16+6, 16+6, 16+6, 16+6,  16+6},  /* IdSel 9,  TULIP */
	};
	common_fixup(5, 9, 5, irq_tab, 0);
}


/*
 * Fixup configuration for MIKASA (NORITAKE is different)
 *
 * Summary @ 0x536:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 0
 * 1        Interrupt Line B from slot 0
 * 2        Interrupt Line C from slot 0
 * 3        Interrupt Line D from slot 0
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line C from slot 1
 * 7        Interrupt Line D from slot 1
 * 8        Interrupt Line A from slot 2
 * 9        Interrupt Line B from slot 2
 *10        Interrupt Line C from slot 2
 *11        Interrupt Line D from slot 2
 *12        NCR 810 SCSI
 *13        Power Supply Fail
 *14        Temperature Warn
 *15        Reserved
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  6       NCR SCSI controller
 *  7       Intel PCI-EISA bridge chip
 * 11       PCI on board slot 0
 * 12       PCI on board slot 1
 * 13       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
static inline void mikasa_fixup(void)
{
	static char irq_tab[8][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+12, 16+12, 16+12, 16+12, 16+12},	/* IdSel 17,  SCSI */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 18,  PCEB */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 19,  ???? */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 20,  ???? */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 21,  ???? */
		{ 16+0,  16+0,  16+1,  16+2,  16+3},	/* IdSel 22,  slot 0 */
		{ 16+4,  16+4,  16+5,  16+6,  16+7},	/* IdSel 23,  slot 1 */
		{ 16+8,  16+8,  16+9, 16+10, 16+11},	/* IdSel 24,  slot 2 */
	};
	common_fixup(6, 13, 5, irq_tab, 0);
}

/*
 * Fixup configuration for NORITAKE (MIKASA is different)
 *
 * Summary @ 0x542, summary register #1:
 * Bit      Meaning
 * 0        All valid ints from summary regs 2 & 3
 * 1        QLOGIC ISP1020A SCSI
 * 2        Interrupt Line A from slot 0
 * 3        Interrupt Line B from slot 0
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line A from slot 2
 * 7        Interrupt Line B from slot 2
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line B from slot 3
 *10        Interrupt Line A from slot 4
 *11        Interrupt Line B from slot 4
 *12        Interrupt Line A from slot 5
 *13        Interrupt Line B from slot 5
 *14        Interrupt Line A from slot 6
 *15        Interrupt Line B from slot 6
 *
 * Summary @ 0x544, summary register #2:
 * Bit      Meaning
 * 0        OR of all unmasked ints in SR #2
 * 1        OR of secondary bus ints
 * 2        Interrupt Line C from slot 0
 * 3        Interrupt Line D from slot 0
 * 4        Interrupt Line C from slot 1
 * 5        Interrupt line D from slot 1
 * 6        Interrupt Line C from slot 2
 * 7        Interrupt Line D from slot 2
 * 8        Interrupt Line C from slot 3
 * 9        Interrupt Line D from slot 3
 *10        Interrupt Line C from slot 4
 *11        Interrupt Line D from slot 4
 *12        Interrupt Line C from slot 5
 *13        Interrupt Line D from slot 5
 *14        Interrupt Line C from slot 6
 *15        Interrupt Line D from slot 6
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  7       Intel PCI-EISA bridge chip
 *  8       DEC PCI-PCI bridge chip
 * 11       PCI on board slot 0
 * 12       PCI on board slot 1
 * 13       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
static inline void noritake_fixup(void)
{
	static char irq_tab[13][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18,  PCEB */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19,  PPB  */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20,  ???? */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21,  ???? */
		{ 16+2,  16+2,  16+3,  32+2,  32+3},  /* IdSel 22,  slot 0 */
		{ 16+4,  16+4,  16+5,  32+4,  32+5},  /* IdSel 23,  slot 1 */
		{ 16+6,  16+6,  16+7,  32+6,  32+7},  /* IdSel 24,  slot 2 */
		/* The following are actually on bus 1, across the bridge */
		{ 16+1,  16+1,  16+1,  16+1,  16+1},  /* IdSel 16,  QLOGIC */
		{ 16+8,  16+8,  16+9,  32+8,  32+9},  /* IdSel 17,  slot 3 */
		{16+10, 16+10, 16+11, 32+10, 32+11},  /* IdSel 18,  slot 4 */
		{16+12, 16+12, 16+13, 32+12, 32+13},  /* IdSel 19,  slot 5 */
		{16+14, 16+14, 16+15, 32+14, 32+15},  /* IdSel 20,  slot 6 */
	};
	common_fixup(7, 18, 5, irq_tab, 0);
}

/*
 * Fixup configuration for ALCOR
 *
 * Summary @ GRU_INT_REQ:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 2
 * 1        Interrupt Line B from slot 2
 * 2        Interrupt Line C from slot 2
 * 3        Interrupt Line D from slot 2
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line C from slot 1
 * 7        Interrupt Line D from slot 1
 * 8        Interrupt Line A from slot 0
 * 9        Interrupt Line B from slot 0
 *10        Interrupt Line C from slot 0
 *11        Interrupt Line D from slot 0
 *12        Interrupt Line A from slot 4
 *13        Interrupt Line B from slot 4
 *14        Interrupt Line C from slot 4
 *15        Interrupt Line D from slot 4
 *16        Interrupt Line D from slot 3
 *17        Interrupt Line D from slot 3
 *18        Interrupt Line D from slot 3
 *19        Interrupt Line D from slot 3
 *20-30     Reserved
 *31        EISA interrupt
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  7       PCI on board slot 0
 *  8       PCI on board slot 3
 *  9       PCI on board slot 4
 * 10       PCEB (PCI-EISA bridge)
 * 11       PCI on board slot 2
 * 12       PCI on board slot 1
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
static inline void alcor_fixup(void)
{
	static char irq_tab[6][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 16+8,  16+8,  16+9, 16+10, 16+11},	/* IdSel 18,  slot 0 */
		{16+16, 16+16, 16+17, 16+18, 16+19},	/* IdSel 19,  slot 3 */
		{16+12, 16+12, 16+13, 16+14, 16+15},	/* IdSel 20,  slot 4 */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 21,  PCEB   */
		{ 16+0,  16+0,  16+1,  16+2,  16+3},	/* IdSel 22,  slot 2 */
		{ 16+4,  16+4,  16+5,  16+6,  16+7},	/* IdSel 23,  slot 1 */
	};
	common_fixup(7, 12, 5, irq_tab, 0);
}

/*
 * Fixup configuration for ALPHA XLT (EV5/EV56)
 *
 * Summary @ GRU_INT_REQ:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 2
 * 1        Interrupt Line B from slot 2
 * 2        Interrupt Line C from slot 2
 * 3        Interrupt Line D from slot 2
 * 4        Interrupt Line A from slot 1
 * 5        Interrupt line B from slot 1
 * 6        Interrupt Line C from slot 1
 * 7        Interrupt Line D from slot 1
 * 8        Interrupt Line A from slot 0
 * 9        Interrupt Line B from slot 0
 *10        Interrupt Line C from slot 0
 *11        Interrupt Line D from slot 0
 *12        NCR810 SCSI in slot 9
 *13        DC-21040 (TULIP) in slot 6
 *14-19     Reserved
 *20-23     Jumpers (interrupt)
 *24-27     Module revision
 *28-30     Reserved
 *31        EISA interrupt
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  6       TULIP
 *  7       PCI on board slot 0
 *  8       none
 *  9       SCSI
 * 10       PCI-ISA bridge
 * 11       PCI on board slot 2
 * 12       PCI on board slot 1
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
static inline void xlt_fixup(void)
{
	static char irq_tab[7][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+13, 16+13, 16+13, 16+13, 16+13},	/* IdSel 17,  TULIP  */
		{ 16+8,  16+8,  16+9, 16+10, 16+11},	/* IdSel 18,  slot 0 */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 19,  none   */
		{16+12, 16+12, 16+12, 16+12, 16+12},	/* IdSel 20,  SCSI   */
		{   -1,    -1,    -1,    -1,    -1},	/* IdSel 21,  SIO    */
		{ 16+0,  16+0,  16+1,  16+2,  16+3},	/* IdSel 22,  slot 2 */
		{ 16+4,  16+4,  16+5,  16+6,  16+7},	/* IdSel 23,  slot 1 */
	};
	common_fixup(6, 12, 5, irq_tab, 0);
}

/*
 * Fixup configuration for ALPHA SABLE (2100) - 2100A is different ??
 *
 * Summary Registers (536/53a/53c):
 * Bit      Meaning
 *-----------------
 * 0        PCI slot 0
 * 1        NCR810 (builtin)
 * 2        TULIP (builtin)
 * 3        mouse
 * 4        PCI slot 1
 * 5        PCI slot 2
 * 6        keyboard
 * 7        floppy
 * 8        COM2
 * 9        parallel port
 *10        EISA irq 3
 *11        EISA irq 4
 *12        EISA irq 5
 *13        EISA irq 6
 *14        EISA irq 7
 *15        COM1
 *16        EISA irq 9
 *17        EISA irq 10
 *18        EISA irq 11
 *19        EISA irq 12
 *20        EISA irq 13
 *21        EISA irq 14
 *22        NC
 *23        IIC
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  0       TULIP
 *  1       SCSI
 *  2       PCI-EISA bridge
 *  3       none
 *  4       none
 *  5       none
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
/*
 * NOTE: the IRQ assignments below are arbitrary, but need to be consistent
 * with the values in the sable_irq_to_mask[] and sable_mask_to_irq[] tables
 * in irq.c
 */

#ifdef CONFIG_ALPHA_SABLE
static inline void sable_fixup(void)
{
        static char irq_tab[9][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 32+0,  32+0,  32+0,  32+0,  32+0},  /* IdSel 0,  TULIP  */
		{ 32+1,  32+1,  32+1,  32+1,  32+1},  /* IdSel 1,  SCSI   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 2,  SIO   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 3,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 4,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 5,  none   */
		{ 32+2,  32+2,  32+2,  32+2,  32+2},  /* IdSel 6,  slot 0 */
		{ 32+3,  32+3,  32+3,  32+3,  32+3},  /* IdSel 7,  slot 1 */
		{ 32+4,  32+4,  32+4,  32+4,  32+4},  /* IdSel 8,  slot 2 */
        };
        common_fixup(0, 8, 5, irq_tab, 0);
}
#endif

/*
 * Fixup configuration for MIATA (EV56+PYXIS)
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        Fan Fault
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        none
 * 4        CID0 (Riser ID)
 * 5        CID1 (Riser ID)
 * 6        Interval timer
 * 7        PCI-ISA Bridge
 * 8        Ethernet
 * 9        EIDE (deprecated, ISA 14/15 used)
 *10        none
 *11        USB
 *12        Interrupt Line A from slot 4
 *13        Interrupt Line B from slot 4
 *14        Interrupt Line C from slot 4
 *15        Interrupt Line D from slot 4
 *16        Interrupt Line A from slot 5
 *17        Interrupt line B from slot 5
 *18        Interrupt Line C from slot 5
 *19        Interrupt Line D from slot 5
 *20        Interrupt Line A from slot 1
 *21        Interrupt Line B from slot 1
 *22        Interrupt Line C from slot 1
 *23        Interrupt Line D from slot 1
 *24        Interrupt Line A from slot 2
 *25        Interrupt Line B from slot 2
 *26        Interrupt Line C from slot 2
 *27        Interrupt Line D from slot 2
 *27        Interrupt Line A from slot 3
 *29        Interrupt Line B from slot 3
 *30        Interrupt Line C from slot 3
 *31        Interrupt Line D from slot 3
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  3       DC21142 Ethernet
 *  4       EIDE CMD646
 *  5       none
 *  6       USB
 *  7       PCI-ISA bridge
 *  8       PCI-PCI Bridge      (SBU Riser)
 *  9       none
 * 10       none
 * 11       PCI on board slot 4 (SBU Riser)
 * 12       PCI on board slot 5 (SBU Riser)
 *
 *  These are behind the bridge, so I'm not sure what to do...
 *
 * 13       PCI on board slot 1 (SBU Riser)
 * 14       PCI on board slot 2 (SBU Riser)
 * 15       PCI on board slot 3 (SBU Riser)
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

#ifdef CONFIG_ALPHA_MIATA
static inline void miata_fixup(void)
{
        static char irq_tab[18][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+ 8, 16+ 8, 16+ 8, 16+ 8, 16+ 8},  /* IdSel 14,  DC21142  */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 15,  EIDE   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 16,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 17,  none   */
/*		{16+11, 16+11, 16+11, 16+11, 16+11},*//* IdSel 17,  USB ??  */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18,  PCI-ISA */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19,  PCI-PCI */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21,  none    */
		{16+12, 16+12, 16+13, 16+14, 16+15},  /* IdSel 22,  slot 4 */
		{16+16, 16+16, 16+17, 16+18, 16+19},  /* IdSel 23,  slot 5 */
		/* The following are actually on bus 1, across the bridge */
		{16+20, 16+20, 16+21, 16+22, 16+23},  /* IdSel 24,  slot 1 */
		{16+24, 16+24, 16+25, 16+26, 16+27},  /* IdSel 25,  slot 2 */
		{16+28, 16+28, 16+29, 16+30, 16+31},  /* IdSel 26,  slot 3 */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 27,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 28,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 29,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 30,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 31,  PCI-PCI */
        };
	common_fixup(3, 20, 5, irq_tab, 0);
	es1888_init();
}
#endif

/*
 * Fixup configuration for SX164 (PCA56+PYXIS)
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        RSVD
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        MBZ
 * 4        RAZ
 * 5        RAZ
 * 6        Interval timer (RTC)
 * 7        PCI-ISA Bridge
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line A from slot 2
 *10        Interrupt Line A from slot 1
 *11        Interrupt Line A from slot 0
 *12        Interrupt Line B from slot 3
 *13        Interrupt Line B from slot 2
 *14        Interrupt Line B from slot 1
 *15        Interrupt line B from slot 0
 *16        Interrupt Line C from slot 3

 *17        Interrupt Line C from slot 2
 *18        Interrupt Line C from slot 1
 *19        Interrupt Line C from slot 0
 *20        Interrupt Line D from slot 3
 *21        Interrupt Line D from slot 2
 *22        Interrupt Line D from slot 1
 *23        Interrupt Line D from slot 0
 *
 * IdSel       
 *   5  32 bit PCI option slot 2
 *   6  64 bit PCI option slot 0
 *   7  64 bit PCI option slot 1
 *   8  Cypress I/O
 *   9  32 bit PCI option slot 3
 * 
 */

#ifdef CONFIG_ALPHA_SX164
static inline void sx164_fixup(void)
{
	static char irq_tab[5][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 16+ 9, 16+ 9, 16+13, 16+17, 16+21}, /* IdSel 5 slot 2 J17 */
		{ 16+11, 16+11, 16+15, 16+19, 16+23}, /* IdSel 6 slot 0 J19 */
		{ 16+10, 16+10, 16+14, 16+18, 16+22}, /* IdSel 7 slot 1 J18 */
		{    -1,    -1,    -1,	  -1,    -1}, /* IdSel 8 SIO        */
		{ 16+ 8, 16+ 8, 16+12, 16+16, 16+20}  /* IdSel 9 slot 3 J15 */
	};

	common_fixup(5, 9, 5, irq_tab, 0);

	SMC669_Init();
}
#endif

/*
 * Fixup configuration for all boards that route the PCI interrupts
 * through the SIO PCI/ISA bridge.  This includes Noname (AXPpci33),
 * Avanti (AlphaStation) and Kenetics's Platform 2000.
 */
static inline void sio_fixup(void)
{
	struct pci_dev *dev;
	/*
	 * The Noname board has 5 PCI slots with each of the 4
	 * interrupt pins routed to different pins on the PCI/ISA
	 * bridge (PIRQ0-PIRQ3).  The table below is based on
	 * information available at:
	 *
	 *   http://ftp.digital.com/pub/DEC/axppci/ref_interrupts.txt
	 *
	 * I have no information on the Avanti interrupt routing, but
	 * the routing seems to be identical to the Noname except
	 * that the Avanti has an additional slot whose routing I'm
	 * unsure of.
	 *
	 * pirq_tab[0] is a fake entry to deal with old PCI boards
	 * that have the interrupt pin number hardwired to 0 (meaning
	 * that they use the default INTA line, if they are interrupt
	 * driven at all).
	 */
	static const char pirq_tab[][5] __initlocaldata = {
#ifdef CONFIG_ALPHA_P2K
		{ 0,  0, -1, -1, -1}, /* idsel  6 (53c810) */
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 1,  1,  2,  3,  0}, /* idsel  8 (slot A) */
		{ 2,  2,  3,  0,  1}, /* idsel  9 (slot B) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 11 (unused) */
		{ 3,  3, -1, -1, -1}, /* idsel 12 (CMD0646) */
#else
		{ 3,  3,  3,  3,  3}, /* idsel  6 (53c810) */ 
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 2,  2, -1, -1, -1}, /* idsel  8 (Noname hack: slot closest to ISA) */
		{-1, -1, -1, -1, -1}, /* idsel  9 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{ 0,  0,  2,  1,  0}, /* idsel 11 KN25_PCI_SLOT0 */
		{ 1,  1,  0,  2,  1}, /* idsel 12 KN25_PCI_SLOT1 */
		{ 2,  2,  1,  0,  2}, /* idsel 13 KN25_PCI_SLOT2 */
		{ 0,  0,  0,  0,  0}, /* idsel 14 AS255 TULIP */
#endif
	};
	const size_t pirq_tab_len = sizeof(pirq_tab)/sizeof(pirq_tab[0]);

	/*
	 * route_tab selects irq routing in PCI/ISA bridge so that:
	 *		PIRQ0 -> irq 15
	 *		PIRQ1 -> irq  9
	 *		PIRQ2 -> irq 10
	 *		PIRQ3 -> irq 11
	 *
	 * This probably ought to be configurable via MILO.  For
	 * example, sound boards seem to like using IRQ 9.
	 */

#if defined(CONFIG_ALPHA_BOOK1)
        /* for the AlphaBook1, NCR810 SCSI is 14, PCMCIA controller is 15 */
        const unsigned int route_tab = 0x0e0f0a0a;

#elif defined(CONFIG_ALPHA_NONAME)
	/*
	 * For UDB, the only available PCI slot must not map to IRQ 9,
	 *  since that's the builtin MSS sound chip. That PCI slot
	 *  will map to PIRQ1 (for INTA at least), so we give it IRQ 15
	 *  instead.
	 *
	 * Unfortunately we have to do this for NONAME as well, since
	 *  they are co-indicated when the platform type "Noname" is
	 *  selected... :-(
	 */
	const unsigned int route_tab = 0x0b0a0f09;
#else
	const unsigned int route_tab = 0x0b0a090f;
#endif

	unsigned int level_bits;
	unsigned char pin, slot;
	int pirq;

	pcibios_write_config_dword(0, PCI_DEVFN(7, 0), 0x60, route_tab);

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */
	level_bits = 0;
	for (dev = pci_devices; dev; dev = dev->next) {
		if ((dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) &&
		    (dev->class >> 8 != PCI_CLASS_BRIDGE_PCMCIA))
			continue;

		dev->irq = 0;
		if (dev->bus->number != 0) {
			struct pci_dev *curr = dev;
			/*
			 * read the pin and do the PCI-PCI bridge
			 * interrupt pin swizzle
			 */
			pcibios_read_config_byte(dev->bus->number, dev->devfn,
						 PCI_INTERRUPT_PIN, &pin);
			/* cope with 0 */
			if (pin == 0)
				pin = 1;
			/* follow the chain of bridges, swizzling as we go */
			do {
				/* swizzle */
				pin = bridge_swizzle(pin, PCI_SLOT(curr->devfn));
				/* move up the chain of bridges */
				curr = curr->bus->self;
			} while (curr->bus->self);
			/* The slot is the slot of the last bridge. */
			slot = PCI_SLOT(curr->devfn);
		} else {
			/* work out the slot */
			slot = PCI_SLOT(dev->devfn);
			/* read the pin */
			pcibios_read_config_byte(dev->bus->number, dev->devfn,
						 PCI_INTERRUPT_PIN, &pin);
		}

		if (slot < 6 || slot >= 6 + pirq_tab_len) {
			printk("bios32.sio_fixup: "
			       "weird, found device %04x:%04x in"
			       " non-existent slot %d!!\n",
			       dev->vendor, dev->device, slot);
			continue;
		}
		pirq = pirq_tab[slot - 6][pin];

		DBG_DEVS(("sio_fixup: bus %d  slot 0x%x  VID 0x%x  DID 0x%x\n"
			  "           int_slot 0x%x  pin 0x%x  pirq 0x%x\n",
			  dev->bus->number, PCI_SLOT(dev->devfn), dev->vendor,
			  dev->device, slot, pin, pirq));
		/*
		 * if it's a VGA, enable its BIOS ROM at C0000
		 */
		if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
			pcibios_write_config_dword(dev->bus->number,
						   dev->devfn,
						   PCI_ROM_ADDRESS,
						   0x000c0000 | PCI_ROM_ADDRESS_ENABLE);
		}
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			continue; /* for now, displays get no IRQ */
		}

		if (pirq < 0) {
			printk("bios32.sio_fixup: "
			       "weird, device %04x:%04x coming in on"
			       " slot %d has no irq line!!\n",
			       dev->vendor, dev->device, slot);
			continue;
		}

		dev->irq = (route_tab >> (8 * pirq)) & 0xff;

#ifndef CONFIG_ALPHA_BOOK1
                /* do not set *ANY* level triggers for AlphaBook1 */
                /* must set the PCI IRQs to level triggered */
                level_bits |= (1 << dev->irq);
#endif /* !CONFIG_ALPHA_BOOK1 */

#if PCI_MODIFY
		/* tell the device: */
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);
#endif

#ifdef CONFIG_ALPHA_BOOK1
                /*
                 * On the AlphaBook1, the PCMCIA chip (Cirrus 6729)
                 * is sensitive to PCI bus bursts, so we must DISABLE
                 * burst mode for the NCR 8xx SCSI... :-(
		 *
		 * Note that the NCR810 SCSI driver must preserve the
		 * setting of the bit in order for this to work.  At the
		 * moment (2.0.29), ncr53c8xx.c does NOT do this, but
		 * 53c7,8xx.c DOES.
                 */
                if (dev->vendor == PCI_VENDOR_ID_NCR &&
                    (dev->device == PCI_DEVICE_ID_NCR_53C810 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C815 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C820 ||
                     dev->device == PCI_DEVICE_ID_NCR_53C825)) {
			unsigned int io_port;
			unsigned char ctest4;

			pcibios_read_config_dword(dev->bus->number,
						  dev->devfn,
						  PCI_BASE_ADDRESS_0,
						  &io_port);
			io_port &= PCI_BASE_ADDRESS_IO_MASK;
			ctest4 = inb(io_port+0x21);
			if (!(ctest4 & 0x80)) {
				printk("AlphaBook1 NCR init: setting"
				       " burst disable\n");
				outb(ctest4 | 0x80, io_port+0x21);
			}
                }
#endif /* CONFIG_ALPHA_BOOK1 */
	} /* end for-devs */

	/*
	 * Now, make all PCI interrupts level sensitive.  Notice:
	 * these registers must be accessed byte-wise.  inw()/outw()
	 * don't work.
	 *
	 * Make sure to turn off any level bits set for IRQs 9,10,11,15,
	 *  so that the only bits getting set are for devices actually found.
	 * Note that we do preserve the remainder of the bits, which we hope
	 *  will be set correctly by ARC/SRM.
	 *
	 * Note: we at least preserve any level-set bits on AlphaBook1
	 */
	level_bits |= ((inb(0x4d0) | (inb(0x4d1) << 8)) & 0x71ff);
	outb((level_bits >> 0) & 0xff, 0x4d0);
	outb((level_bits >> 8) & 0xff, 0x4d1);

#ifdef CONFIG_ALPHA_BOOK1
        {
		unsigned char orig, config;
		/* On the AlphaBook1, make sure that register PR1
		   indicates 1Mb mem */
		outb(0x0f, 0x3ce); orig = inb(0x3cf);   /* read PR5  */
		outb(0x0f, 0x3ce); outb(0x05, 0x3cf);   /* unlock PR0-4 */
		outb(0x0b, 0x3ce); config = inb(0x3cf); /* read PR1 */
		if ((config & 0xc0) != 0xc0) {
			printk("AlphaBook1 VGA init: setting 1Mb memory\n");
			config |= 0xc0;
			outb(0x0b, 0x3ce); outb(config, 0x3cf); /* write PR1 */
		}
		outb(0x0f, 0x3ce); outb(orig, 0x3cf); /* (re)lock PR0-4 */
        }
#endif /* CONFIG_ALPHA_BOOK1 */

#ifndef CONFIG_ALPHA_BOOK1
        /* Do not do IDE init for AlphaBook1 */
        enable_ide(0x26e);
#endif
}


#ifdef CONFIG_TGA_CONSOLE
extern void tga_console_init(void);
#endif /* CONFIG_TGA_CONSOLE */

unsigned long __init
pcibios_fixup(unsigned long mem_start, unsigned long mem_end)
{
#if PCI_MODIFY && !defined(CONFIG_ALPHA_RUFFIAN)
	/*
	 * Scan the tree, allocating PCI memory and I/O space.
	 */
	layout_bus(&pci_root);
#endif
	
	/*
	 * Now is the time to do all those dirty little deeds...
	 */
#if defined(CONFIG_ALPHA_NONAME) || defined(CONFIG_ALPHA_AVANTI) || \
    defined(CONFIG_ALPHA_P2K)
	sio_fixup();
#elif defined(CONFIG_ALPHA_CABRIOLET) || defined(CONFIG_ALPHA_EB164)
	cabriolet_fixup();
#elif defined(CONFIG_ALPHA_PC164) || defined(CONFIG_ALPHA_LX164)
	alphapc164_fixup();
#elif defined(CONFIG_ALPHA_EB66P)
	eb66p_fixup();
#elif defined(CONFIG_ALPHA_EB66)
	eb66_and_eb64p_fixup();
#elif defined(CONFIG_ALPHA_EB64P)
	eb66_and_eb64p_fixup();
#elif defined(CONFIG_ALPHA_MIKASA)
	mikasa_fixup();
#elif defined(CONFIG_ALPHA_ALCOR)
	alcor_fixup();
#elif defined(CONFIG_ALPHA_XLT)
	xlt_fixup();
#elif defined(CONFIG_ALPHA_SABLE)
	sable_fixup();
#elif defined(CONFIG_ALPHA_MIATA)
	miata_fixup();
#elif defined(CONFIG_ALPHA_NORITAKE)
	noritake_fixup();
#elif defined(CONFIG_ALPHA_SX164)
	sx164_fixup();
#elif defined(CONFIG_ALPHA_RUFFIAN)
	/* no fixup needed */
#else
# error "You must tell me what kind of platform you want."
#endif

#ifndef CONFIG_ABSTRACT_CONSOLE
#ifdef CONFIG_TGA_CONSOLE
	tga_console_init();
#endif
#endif

	return mem_start;
}


asmlinkage int sys_pciconfig_read(unsigned long bus, unsigned long dfn,
				  unsigned long off, unsigned long len,
				  unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!suser())
		return -EPERM;

	lock_kernel();
	switch (len) {
	case 1:
		err = pcibios_read_config_byte(bus, dfn, off, &ubyte);
		if (err != PCIBIOS_SUCCESSFUL)
			ubyte = 0xff;
		put_user(ubyte, buf);
		break;
	case 2:
		err = pcibios_read_config_word(bus, dfn, off, &ushort);
		if (err != PCIBIOS_SUCCESSFUL)
			ushort = 0xffff;
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		err = pcibios_read_config_dword(bus, dfn, off, &uint);
		if (err != PCIBIOS_SUCCESSFUL)
			uint = 0xffffffff;
		put_user(uint, (unsigned int *)buf);
		break;
	default:
		err = -EINVAL;
		break;
	}
	unlock_kernel();
	return err;
}


asmlinkage int sys_pciconfig_write(unsigned long bus, unsigned long dfn,
				   unsigned long off, unsigned long len,
				   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	long err = 0;

	if (!suser())
		return -EPERM;

	lock_kernel();
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
	unlock_kernel();
	return err;
}


#ifdef CONFIG_ALPHA_MIATA
/*
 * Init the built-in ES1888 sound chip (SB16 compatible)
 */
static int __init
es1888_init(void)
{
	/* Sequence of IO reads to init the audio controller */
	inb(0x0229);
	inb(0x0229);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x0229);
	inb(0x022b);
	inb(0x0229);
	inb(0x0220); /* This sets the base address to 0x220 */

	/* Sequence to set DMA channels */
	outb(0x01, 0x0226);		/* reset */
	inb(0x0226);			/* pause */
	outb(0x00, 0x0226);		/* release reset */
	while (!(inb(0x022e) & 0x80))	/* wait for bit 7 to assert*/
		continue;
	inb(0x022a);			/* pause */
	outb(0xc6, 0x022c);		/* enable extended mode */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0xb1, 0x022c);		/* setup for write to Interrupt CR */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0x14, 0x022c);		/* set IRQ 5 */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0xb2, 0x022c);		/* setup for write to DMA CR */
	while (inb(0x022c) & 0x80)	/* wait for bit 7 to deassert */
		continue;
	outb(0x18, 0x022c);		/* set DMA channel 1 */
    
	return 0;
}
#endif /* CONFIG_ALPHA_MIATA */

#endif /* CONFIG_PCI */
