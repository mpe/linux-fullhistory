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
  "PCI IO bus 0", "PCI IO bus 1", "PCI IO bus 2", "PCI IO bus 3",
  "PCI IO bus 4", "PCI IO bus 5", "PCI IO bus 6", "PCI IO bus 7"
};

const char *const pci_mem_names[] = {
  "PCI mem bus 0", "PCI mem bus 1", "PCI mem bus 2", "PCI mem bus 3",
  "PCI mem bus 4", "PCI mem bus 5", "PCI mem bus 6", "PCI mem bus 7"
};

const char pci_hae0_name[] = "HAE0";


/*
 * The PCI controler list.
 */

struct pci_controler *hose_head, **hose_tail = &hose_head;
struct pci_controler *pci_isa_hose;

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
	struct pci_dev *dev = data;
	struct pci_controler *hose = dev->sysdata;
	unsigned long alignto;
	unsigned long start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->io_space->start < PCIBIOS_MIN_IO)
			start = PCIBIOS_MIN_IO + hose->io_space->start;

		/*
		 * Aligning to 0x800 rather than the minimum base of
		 * 0x400 is an attempt to avoid having devices in 
		 * any 0x?C?? range, which is where the de4x5 driver
		 * probes for EISA cards.
		 *
		 * Adaptecs, especially, resent such intrusions.
		 *
		 * The de4x5 driver has the eisa probe conditionalized
		 * out for Alpha, so lower the minimum base back to 0x400.
		 */
		alignto = MAX(0x400, size);
		start = ALIGN(start, alignto);
	}
	else if	(res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->mem_space->start < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM + hose->io_space->start;

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
	struct pci_controler *hose = (struct pci_controler *)bus->sysdata;
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;
		if (dev->resource[i].flags & IORESOURCE_IO)
			pcibios_fixup_resource(&dev->resource[i],
					       hose->io_space);
		else if (dev->resource[i].flags & IORESOURCE_MEM)
			pcibios_fixup_resource(&dev->resource[i],
					       hose->mem_space);
	}
	pcibios_assign_special(dev);
}

void __init
pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propogate hose info into the subordinate devices.  */

	struct pci_controler *hose = (struct pci_controler *) bus->sysdata;
	struct list_head *ln;

	/* ???? */
	bus->resource[0] = hose->io_space;
	bus->resource[1] = hose->mem_space;

	/* If this is a bridge, get the current bases */
	if (bus->self) {
		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(bus->self, bus->parent);
	}

	for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
		struct pci_dev *dev = pci_dev_b(ln);
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	struct pci_controler *hose = dev->sysdata;
	int where;
	u32 reg;

	if (resource < PCI_ROM_RESOURCE) 
		where = PCI_BASE_ADDRESS_0 + (resource * 4);
	else if (resource == PCI_ROM_RESOURCE)
		where = dev->rom_base_reg;
	else {
		return; /* Don't update non-standard resources here. */
	}

	/* Point root at the hose root. */
	if (res->flags & IORESOURCE_IO)
		root = hose->io_space;
	if (res->flags & IORESOURCE_MEM)
		root = hose->mem_space;

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
	struct pci_controler *hose = (struct pci_controler *)bus->sysdata;

	ranges->io_start -= hose->io_space->start;
	ranges->io_end -= hose->io_space->start;
	ranges->mem_start -= hose->mem_space->start;
	ranges->mem_end -= hose->mem_space->start;
}

int
pcibios_enable_device(struct pci_dev *dev)
{
	/* Nothing to do, since we enable all devices at startup.  */
	return 0;
}

/*
 *  If we set up a device for bus mastering, we need to check the latency
 *  timer as certain firmware forgets to set it properly, as seen
 *  on SX164 and LX164 with SRM.
 */
void
pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat >= 16) return;
	printk("PCI: Setting latency timer of device %s to 64\n",
							dev->slot_name);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
}

#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))

static void __init
pcibios_size_bridge(struct pci_bus *bus, struct pbus_set_ranges_data *outer)
{
	struct pbus_set_ranges_data inner;
	struct pci_dev *dev;
	struct pci_dev *bridge = bus->self;
	struct pci_controler *hose = bus->sysdata;
	struct list_head *ln;

	if (!bridge)
		return;	/* host bridge, nothing to do */

	/* set reasonable default locations for pcibios_align_resource */
	inner.io_start = hose->io_space->start + PCIBIOS_MIN_IO;
	inner.mem_start = hose->mem_space->start + PCIBIOS_MIN_MEM;
	inner.io_end = inner.io_start;
	inner.mem_end = inner.mem_start;

	/* Collect information about how our direct children are layed out. */
	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		int i;
		dev = pci_dev_b(ln);

		/* Skip bridges for now */
		if (dev->class >> 8 == PCI_CLASS_BRIDGE_PCI)
			continue;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource res;
			unsigned long size;

			memcpy(&res, &dev->resource[i], sizeof(res));
			size = res.end - res.start + 1;

			if (res.flags & IORESOURCE_IO) {
				res.start = inner.io_end;
				pcibios_align_resource(dev, &res, size);
				inner.io_end = res.start + size;
			} else if (res.flags & IORESOURCE_MEM) {
				res.start = inner.mem_end;
				pcibios_align_resource(dev, &res, size);
				inner.mem_end = res.start + size;
			}
		}
	}

	/* And for all of the subordinate busses. */
	for (ln=bus->children.next; ln != &bus->children; ln=ln->next)
		pcibios_size_bridge(pci_bus_b(ln), &inner);

	/* turn the ending locations into sizes (subtract start) */
	inner.io_end -= inner.io_start;
	inner.mem_end -= inner.mem_start;

	/* Align the sizes up by bridge rules */
	inner.io_end = ROUND_UP(inner.io_end, 4*1024) - 1;
	inner.mem_end = ROUND_UP(inner.mem_end, 1*1024*1024) - 1;

	/* Adjust the bridge's allocation requirements */
	bridge->resource[0].end = bridge->resource[0].start + inner.io_end;
	bridge->resource[1].end = bridge->resource[1].start + inner.mem_end;

	bridge->resource[PCI_BRIDGE_RESOURCES].end =
	    bridge->resource[PCI_BRIDGE_RESOURCES].start + inner.io_end;
	bridge->resource[PCI_BRIDGE_RESOURCES+1].end =
	    bridge->resource[PCI_BRIDGE_RESOURCES+1].start + inner.mem_end;

	/* adjust parent's resource requirements */
	if (outer) {
		outer->io_end = ROUND_UP(outer->io_end, 4*1024);
		outer->io_end += inner.io_end;

		outer->mem_end = ROUND_UP(outer->mem_end, 1*1024*1024);
		outer->mem_end += inner.mem_end;
	}
}

#undef ROUND_UP

static void __init 
pcibios_size_bridges(void)
{
	struct list_head *ln1, *ln2;

	for(ln1=pci_root_buses.next; ln1 != &pci_root_buses; ln1=ln1->next)
		for(ln2 = pci_bus_b(ln1)->children.next; 
		    ln2 != &pci_bus_b(ln1)->children;
		    ln2 = ln2->next)
			pcibios_size_bridge(pci_bus_b(ln2), NULL);
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

	pcibios_size_bridges();
	pci_assign_unassigned_resources();
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


/* Provide information on locations of various I/O regions in physical
   memory.  Do this on a per-card basis so that we choose the right hose.  */

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long dfn)
{
	struct pci_controler *hose;
	struct pci_dev *dev;

	/* Special hook for ISA access.  */
	if (bus == 0 && dfn == 0) {
		hose = pci_isa_hose;
	} else {
		dev = pci_find_slot(bus, dfn);
		if (!dev)
			return -ENODEV;
		hose = dev->sysdata;
	}

	switch (which) {
	case IOBASE_HOSE:
		return hose->index;
	case IOBASE_SPARSE_MEM:
		return hose->sparse_mem_base;
	case IOBASE_DENSE_MEM:
		return hose->dense_mem_base;
	case IOBASE_SPARSE_IO:
		return hose->sparse_io_base;
	case IOBASE_DENSE_IO:
		return hose->dense_io_base;
	}

	return -EOPNOTSUPP;
}
