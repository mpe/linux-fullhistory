/*
 *	linux/arch/alpha/kernel/pci.c
 *
 * Extruded from code written by
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 */

/* 2.3.x PCI/resources, 1999 Andrea Arcangeli <andrea@suse.de> */

#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <asm/machvec.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * Some string constants used by the various core logics. 
 */

const char *const pci_io_names[] = {
	"PCI IO bus 0", "PCI IO bus 1", "PCI IO bus 2", "PCI IO bus 3"
};

const char *const pci_mem_names[] = {
	"PCI mem bus 0", "PCI mem bus 1", "PCI mem bus 2", "PCI mem bus 3"
};

const char pci_hae0_name[] = "HAE0";


/*
 * The PCI controler list.
 */

struct pci_controler *hose_head, **hose_tail = &hose_head;

/*
 * Quirks.
 */

static void __init
quirk_eisa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_EISA;
}

static void __init
quirk_isa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_ISA;
}

static void __init
quirk_ali_ide_ports(struct pci_dev *dev)
{
	if (dev->resource[0].end == 0xffff)
		dev->resource[0].end = dev->resource[0].start + 7;
	if (dev->resource[2].end == 0xffff)
		dev->resource[2].end = dev->resource[2].start + 7;
	if (dev->resource[3].end == 0xffff)
		dev->resource[3].end = dev->resource[3].start + 7;
}

static void __init
quirk_vga_enable_rom(struct pci_dev *dev)
{
	/* If it's a VGA, enable its BIOS ROM at C0000.
	   But if its a Cirrus 543x/544x DISABLE it, since
	   enabling ROM disables the memory... */
	if ((dev->class >> 8) == PCI_CLASS_DISPLAY_VGA &&
	    /* But if its a Cirrus 543x/544x DISABLE it */
	    (dev->vendor != PCI_VENDOR_ID_CIRRUS ||
	     (dev->device < 0x00a0) || (dev->device > 0x00ac)))
	{
		u32 reg;

		pci_read_config_dword(dev, dev->rom_base_reg, &reg);
		reg |= PCI_ROM_ADDRESS_ENABLE;
		pci_write_config_dword(dev, dev->rom_base_reg, reg);
		dev->resource[PCI_ROM_RESOURCE].flags |= PCI_ROM_ADDRESS_ENABLE;
	}
}

struct pci_fixup pcibios_fixups[] __initdata = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82375,
	  quirk_eisa_bridge },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82378,
	  quirk_isa_bridge },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5229,
	  quirk_ali_ide_ports },
	{ PCI_FIXUP_FINAL, PCI_ANY_ID, PCI_ANY_ID, quirk_vga_enable_rom },
	{ 0 }
};

#define MAX(val1, val2)		((val1) > (val2) ? (val1) : (val2))
#define ALIGN(val,align)	(((val) + ((align) - 1)) & ~((align) - 1))
#define KB			1024
#define MB			(1024*KB)
#define GB			(1024*MB)

void
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
	struct pci_dev * dev = data;
	unsigned long alignto;
	unsigned long start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/*
		 * Aligning to 0x800 rather than the minimum base of
		 * 0x400 is an attempt to avoid having devices in 
		 * any 0x?C?? range, which is where the de4x5 driver
		 * probes for EISA cards.
		 *
		 * Adaptecs, especially, resent such intrusions.
		 */
		alignto = MAX(0x800, size);
		start = ALIGN(start, alignto);
	}
	else if	(res->flags & IORESOURCE_MEM) {
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

		/* Align to multiple of size of minimum base.  */
		alignto = MAX(0x1000, size);
		start = ALIGN(start, alignto);
		if (size <= 7 * 16*MB) {
			if (((start / (16*MB)) & 0x7) == 0) {
				start &= ~(128*MB - 1);
				start += 16*MB;
				start  = ALIGN(start, alignto);
			}
			if (start/(128*MB) != (start + size)/(128*MB)) {
				start &= ~(128*MB - 1);
				start += (128 + 16)*MB;
				start  = ALIGN(start, alignto);
			}
		}
	}

	res->start = start;
}
#undef MAX
#undef ALIGN
#undef KB
#undef MB
#undef GB

/* 
 * Pre-layout host-independant device initialization.
 */

static void __init
pcibios_assign_special(struct pci_dev * dev)
{
	int i;

	/* The first three resources of an IDE controler are often magic, 
	   so leave them unchanged.  This is true, for instance, of the
	   Contaq 82C693 as seen on SX164 and DP264.  */

	if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE) {
		/* Resource 1 of IDE controller is the address of HD_CMD
		   register which actually occupies a single byte (0x3f6
		   for ide0) in reported 0x3f4-3f7 range. We have to fix
		   that to avoid resource conflict with AT-style floppy
		   controller. */
		dev->resource[1].start += 2;
		dev->resource[1].end = dev->resource[1].start;
		for (i = 0; i < PCI_NUM_RESOURCES; i++)
			if (dev->resource[i].flags && dev->resource[i].start)
				pci_claim_resource(dev, i);
	}
	/*
	 * We don't have code that will init the CYPRESS bridge correctly
	 * so we do the next best thing, and depend on the previous
	 * console code to do the right thing, and ignore it here... :-\
	 */
	else if (dev->vendor == PCI_VENDOR_ID_CONTAQ &&
		 dev->device == PCI_DEVICE_ID_CONTAQ_82C693)
		for (i = 0; i < PCI_NUM_RESOURCES; i++)
			if (dev->resource[i].flags && dev->resource[i].start)
				pci_claim_resource(dev, i);
}


void __init
pcibios_init(void)
{
	if (!alpha_mv.init_pci)
		return;
	alpha_mv.init_pci();
}

char * __init
pcibios_setup(char *str)
{
	return str;
}

void __init
pcibios_fixup_resource(struct resource *res, struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

void __init
pcibios_fixup_device_resources(struct pci_dev *dev, struct pci_bus *bus)
{
	/* Update device resources.  */

	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;
		if (dev->resource[i].flags & IORESOURCE_IO)
			pcibios_fixup_resource(&dev->resource[i],
					       bus->resource[0]);
		else if (dev->resource[i].flags & IORESOURCE_MEM)
			pcibios_fixup_resource(&dev->resource[i],
					       bus->resource[1]);
	}
	pcibios_assign_special(dev);
}

void __init
pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propogate hose info into the subordinate devices.  */

	struct pci_controler *hose = (struct pci_controler *) bus->sysdata;
	struct pci_dev *dev;

	bus->resource[0] = hose->io_space;
	bus->resource[1] = hose->mem_space;
	for (dev = bus->devices; dev; dev = dev->sibling) {
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	int where;
	u32 reg;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	reg = (res->start - root->start) | (res->flags & 0xf);
	pci_write_config_dword(dev, where, reg);
	if ((res->flags & (PCI_BASE_ADDRESS_SPACE
			   | PCI_BASE_ADDRESS_MEM_TYPE_MASK))
	    == (PCI_BASE_ADDRESS_SPACE_MEMORY
		| PCI_BASE_ADDRESS_MEM_TYPE_64)) {
		pci_write_config_dword(dev, where+4, 0);
		printk(KERN_WARNING "PCI: dev %s type 64-bit\n", dev->name);
	}

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

/* Most Alphas have straight-forward swizzling needs.  */

u8 __init
common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	struct pci_controler *hose = dev->sysdata;

	if (dev->bus->number != hose->first_busno) {
		u8 pin = *pinp;
		do {
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
			/* Move up the chain of bridges. */
			dev = dev->bus->self;
		} while (dev->bus->self);
		*pinp = pin;

		/* The slot is the slot of the last bridge. */
	}

	return PCI_SLOT(dev->devfn);
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus * bus,
			  struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

void __init
common_init_pci(void)
{
	struct pci_controler *hose;
	struct pci_bus *bus;
	int next_busno;

	/* Scan all of the recorded PCI controlers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {
		hose->first_busno = next_busno;
		hose->last_busno = 0xff;
		bus = pci_scan_bus(next_busno, alpha_mv.pci_ops, hose);
		hose->bus = bus;
		next_busno = hose->last_busno = bus->subordinate;
		next_busno += 1;
	}

	pci_assign_unassigned_resources(alpha_mv.min_io_address,
				        alpha_mv.min_mem_address);
	pci_fixup_irqs(alpha_mv.pci_swizzle, alpha_mv.pci_map_irq);
	pci_set_bus_ranges();
}


struct pci_controler * __init
alloc_pci_controler(void)
{
	struct pci_controler *hose;

	hose = alloc_bootmem(sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(void)
{
	struct resource *res;

	res = alloc_bootmem(sizeof(*res));

	return res;
}
