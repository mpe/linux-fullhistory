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
			/* this base-address register is unused */
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
 * The SRM console *disables* the IDE interface, this code ensures its
 * enabled.
 *
 * This code bangs on a control register of the 87312 Super I/O chip
 * that implements parallel port/serial ports/IDE/FDI.  Depending on
 * the motherboard, the Super I/O chip can be configured through a
 * pair of registers that are located either at I/O ports 0x26e/0x26f
 * or 0x398/0x399.  Unfortunately, autodetecting which base address is
 * in use works only once (right after a reset).  The Super I/O chip
 * has the additional quirk that configuration register data must be
 * written twice (I believe this is a saftey feature to prevent
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
 * Most evaluation boards share most of the fixup code, which is isolated here.
 * This function is declared "inline" as only one platform will ever be selected
 * in any given kernel.  If that platform doesn't need this code, we don't want
 * it around as dead code.
 */
static inline void common_fixup(long min_idsel, long max_idsel, long irqs_per_slot,
				char irq_tab[max_idsel - min_idsel + 1][irqs_per_slot],
				long ide_base)
{
	struct pci_dev *dev;
	unsigned char pin;
	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */
	for (dev = pci_devices; dev; dev = dev->next) {
		dev->irq = 0;
		/*
		 * Ignore things not on the primary bus - I'll figure
		 * this out one day - Dave Rusling
		 */
		if (dev->bus->number != 0)
			continue;

		/* read the pin */
		pcibios_read_config_byte(dev->bus->number, dev->devfn,
					 PCI_INTERRUPT_PIN, &pin);
		if (irq_tab[PCI_SLOT(dev->devfn) - min_idsel][pin] != -1)
			dev->irq = irq_tab[PCI_SLOT(dev->devfn) - min_idsel][pin];
#if PCI_MODIFY
		/* tell the device: */
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);
#endif
		/*
		 * if its a VGA, enable its BIOS ROM at C0000
		 */
		if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
			pcibios_write_config_dword(dev->bus->number, dev->devfn,
						   PCI_ROM_ADDRESS,
						   0x000c0000 | PCI_ROM_ADDRESS_ENABLE);
		}
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
	char irq_tab[5][5] = {
		{16+0, 16+0, 16+5,  16+9, 16+13},	/* IdSel 6,  slot 0, J25 */
		{16+1, 16+1, 16+6, 16+10, 16+14},	/* IdSel 7,  slot 1, J26 */
		{  -1,   -1,   -1,    -1,    -1},	/* IdSel 8,  SIO         */
		{16+2, 16+2, 16+7, 16+11, 16+15},	/* IdSel 9,  slot 2, J27 */
		{16+3, 16+3, 16+8, 16+12,  16+6}	/* IdSel 10, slot 3, J28 */
	};
	common_fixup(6, 10, 5, irq_tab, 0x398);
}


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
	char irq_tab[5][5] = {
		{ 16+2, 16+2, 16+7, 16+11, 16+15},      /* IdSel 5,  slot 2, J21 */
		{ 16+0, 16+0, 16+5,  16+9, 16+13},      /* IdSel 6,  slot 0, J19 */
		{ 16+1, 16+1, 16+6, 16+10, 16+14},      /* IdSel 7,  slot 1, J20 */
		{   -1,   -1,   -1,    -1,    -1},	/* IdSel 8,  SIO         */
		{ 16+3, 16+3, 16+8, 16+12, 16+16}       /* IdSel 9,  slot 3, J22 */
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
	char irq_tab[5][5] = {
		{16+7, 16+7, 16+7, 16+7,  16+7},	/* IdSel 5,  slot ?, ?? */
		{16+0, 16+0, 16+2, 16+4,  16+9},	/* IdSel 6,  slot ?, ?? */
		{16+1, 16+1, 16+3, 16+8, 16+10},	/* IdSel 7,  slot ?, ?? */
		{  -1,   -1,   -1,   -1,    -1},	/* IdSel 8,  SIO */
		{16+6, 16+6, 16+6, 16+6,  16+6},	/* IdSel 9,  TULIP */
	};
	common_fixup(5, 9, 5, irq_tab, 0);
}


/*
 * Fixup configuration for Noname (AXPpci33) and Avanti (AlphaStation 240).
 */
static inline void avanti_and_noname_fixup(void)
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
	static const char pirq_tab[][5] = {
		{ 3,  3,  3,  3,  3}, /* idsel  6 (53c810) */ 
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 2,  2, -1, -1, -1}, /* idsel  8 (slot closest to ISA) */
		{-1, -1, -1, -1, -1}, /* idsel  9 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{ 0,  0,  2,  1,  0}, /* idsel 11 (slot furthest from ISA) KN25_PCI_SLOT0 */
		{ 1,  1,  0,  2,  1}, /* idsel 12 (middle slot) KN25_PCI_SLOT1 */
#ifdef CONFIG_ALPHA_AVANTI
		{ 1,  1, -1, -1, -1}, /* idsel 13 KN25_PCI_SLOT2 */
#endif /* CONFIG_ALPHA_AVANTI */
	};
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
	const unsigned int route_tab = 0x0b0a090f;
	unsigned int level_bits;
	unsigned char pin;
	int pirq;

	pcibios_write_config_dword(0, PCI_DEVFN(7, 0), 0x60, route_tab);

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */
	level_bits = 0;
	for (dev = pci_devices; dev; dev = dev->next) {
		dev->irq = 0;
		if (dev->bus->number != 0 ||
		    PCI_SLOT(dev->devfn) < 6 ||
		    PCI_SLOT(dev->devfn) >= 6 + sizeof(pirq_tab)/sizeof(pirq_tab[0]))
		{
			printk("bios32.avanti_and_noname_fixup: no dev on bus %d, slot %d!!\n",
			       dev->bus->number, PCI_SLOT(dev->devfn));
			continue;
		}
		pcibios_read_config_byte(dev->bus->number, dev->devfn,
					 PCI_INTERRUPT_PIN, &pin);
		pirq = pirq_tab[PCI_SLOT(dev->devfn) - 6][pin];
		if (pirq < 0) {
			continue;
		}
		/*
		 * if its a VGA, enable its BIOS ROM at C0000
		 */
		if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA) {
			pcibios_write_config_dword(dev->bus->number, dev->devfn,
						   PCI_ROM_ADDRESS,
						   0x000c0000 | PCI_ROM_ADDRESS_ENABLE);
		}
		if ((dev->class >> 16) == PCI_BASE_CLASS_DISPLAY) {
			continue; /* for now, displays get no IRQ */
		}
		dev->irq = (route_tab >> (8 * pirq)) & 0xff;

		/* must set the PCI IRQs to level triggered */
		level_bits |= (1 << dev->irq);

#if PCI_MODIFY
		/* tell the device: */
		pcibios_write_config_byte(dev->bus->number, dev->devfn,
					  PCI_INTERRUPT_LINE, dev->irq);
#endif
	}
	/* now, set any level-triggered IRQs */
	if (level_bits)
		outw(level_bits, 0x4d0);


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

	enable_ide(0x26e);
}


#ifdef CONFIG_TGA_CONSOLE
extern void tga_console_init(void);
#endif /* CONFIG_TGA_CONSOLE */

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
#if defined(CONFIG_ALPHA_NONAME) || defined(CONFIG_ALPHA_AVANTI)
	avanti_and_noname_fixup();
#elif defined(CONFIG_ALPHA_CABRIOLET)
	cabriolet_fixup();
#elif defined(CONFIG_ALPHA_EB66P)
	eb66p_fixup();
#elif defined(CONFIG_ALPHA_EB66)
	eb66_and_eb64p_fixup();
#elif defined(CONFIG_ALPHA_EB64P)
	eb66_and_eb64p_fixup();
#else
#	error You must tell me what kind of platform you want.
#endif

#ifdef CONFIG_TGA_CONSOLE
        tga_console_init();
#endif /* CONFIG_TGA_CONSOLE */

	return mem_start;
}


const char *pcibios_strerror (int error)
{
        static char buf[80];

        switch (error) {
                case PCIBIOS_SUCCESSFUL:
                        return "SUCCESSFUL";

                case PCIBIOS_FUNC_NOT_SUPPORTED:
                        return "FUNC_NOT_SUPPORTED";

                case PCIBIOS_BAD_VENDOR_ID:
                        return "SUCCESSFUL";

                case PCIBIOS_DEVICE_NOT_FOUND:
                        return "DEVICE_NOT_FOUND";

                case PCIBIOS_BAD_REGISTER_NUMBER:
                        return "BAD_REGISTER_NUMBER";

                default:
                        sprintf (buf, "UNKNOWN RETURN 0x%x", error);
                        return buf;
        }
}

#endif /* CONFIG_PCI */
