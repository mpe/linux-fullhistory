/*
 * BK Id: SCCS/s.pci.c 1.35 11/13/01 08:19:57 trini
 */
/*
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/bootmem.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>
#include <asm/uaccess.h>

#include "pci.h"

#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

unsigned long isa_io_base     = 0;
unsigned long isa_mem_base    = 0;
unsigned long pci_dram_offset = 0;

void pcibios_make_OF_bus_map(void);

static void pcibios_fixup_resources(struct pci_dev* dev);
static void fixup_broken_pcnet32(struct pci_dev* dev);
static void fixup_rev1_53c810(struct pci_dev* dev);
#ifdef CONFIG_ALL_PPC
static void pcibios_fixup_cardbus(struct pci_dev* dev);
static u8* pci_to_OF_bus_map;
#endif

/* By default, we don't re-assign bus numbers. We do this only on
 * some pmacs
 */
int pci_assign_all_busses;

struct pci_controller* hose_head;
struct pci_controller** hose_tail = &hose_head;

static int pci_bus_count;

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_TRIDENT,	PCI_ANY_ID,			fixup_broken_pcnet32 },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_NCR,	PCI_DEVICE_ID_NCR_53C810,	fixup_rev1_53c810 },
	{ PCI_FIXUP_HEADER,	PCI_ANY_ID,		PCI_ANY_ID,			pcibios_fixup_resources },
#ifdef CONFIG_ALL_PPC
	/* We should add per-machine fixup support in xxx_setup.c or xxx_pci.c */
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_TI,	PCI_DEVICE_ID_TI_1211,		pcibios_fixup_cardbus }, 
#endif /* CONFIG_ALL_PPC */
 	{ 0 }
};

static void
fixup_rev1_53c810(struct pci_dev* dev)
{
	/* rev 1 ncr53c810 chips don't set the class at all which means
	 * they don't get their resources remapped. Fix that here.
	 */

	if ((dev->class == PCI_CLASS_NOT_DEFINED)) {
		printk("NCR 53c810 rev 1 detected, setting PCI class.\n");
		dev->class = PCI_CLASS_STORAGE_SCSI;
	}
}

static void
fixup_broken_pcnet32(struct pci_dev* dev)
{
	if ((dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET)) {
		dev->vendor = PCI_VENDOR_ID_AMD;
		pci_write_config_word(dev, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
		pci_name_device(dev);
	}
}

void
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			     struct resource *res, int resource)
{
	u32 new, check;
	int reg;
	struct pci_controller* hose = dev->sysdata;
	unsigned long io_offset;
	
	new = res->start;
	if (hose && res->flags & IORESOURCE_IO) {
		io_offset = (unsigned long)hose->io_base_virt - isa_io_base;
		new -= io_offset;
	}
	if (hose && res->flags & IORESOURCE_MEM)
		new -= hose->pci_mem_offset;
	new |= (res->flags & PCI_REGION_FLAG_MASK);
	if (resource < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4*resource;
	} else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}

	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);
	if ((new ^ check) & ((new & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK)) {
		printk(KERN_ERR "PCI: Error while updating region "
		       "%s/%d (%08x != %08x)\n", dev->slot_name, resource,
		       new, check);
	}
}

static void
pcibios_fixup_resources(struct pci_dev *dev)
{
	struct pci_controller* hose = (struct pci_controller *)dev->sysdata;
	int i;
	unsigned long offset;

	if (!hose) {
		printk(KERN_ERR "No hose for PCI dev %s!\n", dev->slot_name);
		return;
	}
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = dev->resource + i;
		if (!res->start || !res->flags)
			continue;
		if (res->end == 0xffffffff) {
			DBG("PCI:%s Resource %d [%08lx-%08lx] is unassigned\n",
			    dev->slot_name, i, res->start, res->end);
			res->end -= res->start;
			res->start = 0;
			continue;
		}
		offset = 0;
		if (res->flags & IORESOURCE_MEM) {
			offset = hose->pci_mem_offset;
		} else if (res->flags & IORESOURCE_IO) {
			offset = (unsigned long) hose->io_base_virt
				- isa_io_base;
		}
		if (offset != 0) {
			res->start += offset;
			res->end += offset;
#ifdef DEBUG
			printk("Fixup res %d (%lx) of dev %s: %lx -> %lx\n",
			       i, res->flags, dev->slot_name,
			       res->start - offset, res->start);
#endif
		}
	}
}

#ifdef CONFIG_ALL_PPC
static void
pcibios_fixup_cardbus(struct pci_dev* dev)
{
	/*
	 * Fix the interrupt routing on the TI1211 chip on the 1999
	 * G3 powerbook, which doesn't get initialized properly by OF.
	 */
	if (dev->vendor == PCI_VENDOR_ID_TI
	    && dev->device == PCI_DEVICE_ID_TI_1211) {
		u32 val;
		/* 0x8c == TI122X_IRQMUX, 2 says to route the INTA
		   signal out the MFUNC0 pin */
		if (pci_read_config_dword(dev, 0x8c, &val) == 0
		    && val == 0)
			pci_write_config_dword(dev, 0x8c, 2);
	}
}
#endif /* CONFIG_ALL_PPC */

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might have be mirrored at 0x0100-0x03ff..
 */
void
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		if (size > 0x100) {
			printk(KERN_ERR "PCI: I/O Region %s/%d too large"
			       " (%ld bytes)\n", dev->slot_name,
			       dev->resource - res, size);
		}

		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	}
}


/*
 *  Handle resources of PCI devices.  If the world were perfect, we could
 *  just allocate all the resource regions and do nothing more.  It isn't.
 *  On the other hand, we cannot just re-allocate all devices, as it would
 *  require us to know lots of host bridge internals.  So we attempt to
 *  keep as much of the original configuration as possible, but tweak it
 *  when it's found to be wrong.
 *
 *  Known BIOS problems we have to work around:
 *	- I/O or memory regions not configured
 *	- regions configured, but not enabled in the command register
 *	- bogus I/O addresses above 64K used
 *	- expansion ROMs left enabled (this may sound harmless, but given
 *	  the fact the PCI specs explicitly allow address decoders to be
 *	  shared between expansion ROMs and other resource regions, it's
 *	  at least dangerous)
 *
 *  Our solution:
 *	(1) Allocate resources for all buses behind PCI-to-PCI bridges.
 *	    This gives us fixed barriers on where we can allocate.
 *	(2) Allocate resources for all enabled devices.  If there is
 *	    a collision, just mark the resource as unallocated. Also
 *	    disable expansion ROMs during this step.
 *	(3) Try to allocate resources for disabled devices.  If the
 *	    resources were assigned correctly, everything goes well,
 *	    if they weren't, they won't disturb allocation of other
 *	    resources.
 *	(4) Assign new addresses to resources which were either
 *	    not configured at all or misconfigured.  If explicitly
 *	    requested by the user, configure expansion ROM address
 *	    as well.
 */

static void __init
pcibios_allocate_bus_resources(struct list_head *bus_list)
{
	struct list_head *ln;
	struct pci_bus *bus;
	int i;
	struct resource *res, *pr;

	/* Depth-First Search on bus tree */
	for (ln = bus_list->next; ln != bus_list; ln=ln->next) {
		bus = pci_bus_b(ln);
		for (i = 0; i < 4; ++i) {
			if ((res = bus->resource[i]) == NULL || !res->flags)
				continue;
			if (bus->parent == NULL)
				pr = (res->flags & IORESOURCE_IO)?
					&ioport_resource: &iomem_resource;
			else
				pr = pci_find_parent_resource(bus->self, res);

			if (pr && request_resource(pr, res) == 0)
				continue;
			printk(KERN_ERR "PCI: Cannot allocate resource region "
			       "%d of PCI bridge %d\n", i, bus->number);
			DBG("PCI: resource is %lx..%lx (%lx), parent %p\n",
			    res->start, res->end, res->flags, pr);
		}
		pcibios_allocate_bus_resources(&bus->children);
	}
}

static inline void alloc_resource(struct pci_dev *dev, int idx)
{
	struct resource *pr, *r = &dev->resource[idx];

	DBG("PCI:%s: Resource %d: %08lx-%08lx (f=%lx)\n",
	    dev->slot_name, idx, r->start, r->end, r->flags);
	pr = pci_find_parent_resource(dev, r);
	if (!pr || request_resource(pr, r) < 0) {
		printk(KERN_ERR "PCI: Cannot allocate resource region %d"
		       " of device %s\n", idx, dev->slot_name);
		if (pr)
			DBG("PCI:  parent is %p: %08lx-%08lx (f=%lx)\n",
			    pr, pr->start, pr->end, pr->flags);
		/* We'll assign a new address later */
		r->end -= r->start;
		r->start = 0;
	}
}

static void __init
pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev;
	int idx, disabled;
	u16 command;
	struct resource *r;

	pci_for_each_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for (idx = 0; idx < 6; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->start)		/* Not assigned at all */
				continue;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled)
				alloc_resource(dev, idx);
		}
		if (pass)
			continue;
		r = &dev->resource[PCI_ROM_RESOURCE];
		if (r->flags & PCI_ROM_ADDRESS_ENABLE) {
			/* Turn the ROM off, leave the resource region, but keep it unregistered. */
			u32 reg;
			DBG("PCI: Switching off ROM of %s\n", dev->slot_name);
			r->flags &= ~PCI_ROM_ADDRESS_ENABLE;
			pci_read_config_dword(dev, dev->rom_base_reg, &reg);
			pci_write_config_dword(dev, dev->rom_base_reg,
					       reg & ~PCI_ROM_ADDRESS_ENABLE);
		}
	}
}

static void __init
pcibios_assign_resources(void)
{
	struct pci_dev *dev;
	int idx;
	struct resource *r;

	pci_for_each_dev(dev) {
		int class = dev->class >> 8;

		/* Don't touch classless devices and host bridges */
		if (!class || class == PCI_CLASS_BRIDGE_HOST)
			continue;

		for (idx = 0; idx < 6; idx++) {
			r = &dev->resource[idx];

			/*
			 * We shall assign a new address to this resource,
			 * either because the BIOS (sic) forgot to do so
			 * or because we have decided the old address was
			 * unusable for some reason.
			 */
			if (!r->start && r->end &&
			    (!ppc_md.pcibios_enable_device_hook ||
			     !ppc_md.pcibios_enable_device_hook(dev, 1)))
				pci_assign_resource(dev, idx);
		}

#if 0 /* don't assign ROMs */
		r = &dev->resource[PCI_ROM_RESOURCE];
		r->end -= r->start;
		r->start = 0;
		if (r->end)
			pci_assign_resource(dev, PCI_ROM_RESOURCE);
#endif
	}
}


int
pcibios_enable_resources(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for(idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n", dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

static int next_controller_index;

struct pci_controller * __init
pcibios_alloc_controller(void)
{
	struct pci_controller *hose;

	hose = (struct pci_controller *)alloc_bootmem(sizeof(*hose));
	memset(hose, 0, sizeof(struct pci_controller));
	
	*hose_tail = hose;
	hose_tail = &hose->next;

	hose->index = next_controller_index++;

	return hose;
}

#ifdef CONFIG_ALL_PPC
/*
 * Functions below are used on OpenFirmware machines.
 */
static void
make_one_node_map(struct device_node* node, u8 pci_bus)
{
	int *bus_range;
	int len;
	
	if (pci_bus >= pci_bus_count)
		return;
	bus_range = (int *) get_property(node, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int)) {
		printk(KERN_WARNING "Can't get bus-range for %s\n",
			       node->full_name);
		return;
	}
	pci_to_OF_bus_map[pci_bus] = bus_range[0];
	
	for (node=node->child; node != 0;node = node->sibling) {
		struct pci_dev* dev;
		unsigned int *class_code, *reg;
		
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if (!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS))
			continue;
		reg = (unsigned int *)get_property(node, "reg", 0);
		if (!reg)
			continue;
		dev = pci_find_slot(pci_bus, ((reg[0] >> 8) & 0xff));
		if (!dev || !dev->subordinate)
			continue;
		make_one_node_map(node, dev->subordinate->number);
	}
}
		
void
pcibios_make_OF_bus_map(void)
{
	int i;
	struct pci_controller* hose;
	u8* of_prop_map;
	
	pci_to_OF_bus_map = (u8*)kmalloc(pci_bus_count, GFP_KERNEL);
	if (!pci_to_OF_bus_map) {
		printk(KERN_ERR "Can't allocate OF bus map !\n");
		return;
	}
	
	/* We fill the bus map with invalid values, that helps
	 * debugging.
	 */
	for (i=0; i<pci_bus_count; i++)
		pci_to_OF_bus_map[i] = 0xff;
	
	/* For each hose, we begin searching bridges */
	for(hose=hose_head; hose; hose=hose->next) {
		struct device_node* node;		
		node = (struct device_node *)hose->arch_data;
		if (!node)
			continue;
		make_one_node_map(node, hose->first_busno);
	}
	of_prop_map = get_property(find_path_device("/"), "pci-OF-bus-map", 0);
	if (of_prop_map)
		memcpy(of_prop_map, pci_to_OF_bus_map, pci_bus_count);
#ifdef DEBUG
	printk("PCI->OF bus map:\n");
	for (i=0; i<pci_bus_count; i++) {
		if (pci_to_OF_bus_map[i] == 0xff)
			continue;
		printk("%d -> %d\n", i, pci_to_OF_bus_map[i]);
	}
#endif	
}

static struct device_node*
scan_OF_childs_for_device(struct device_node* node, u8 bus, u8 dev_fn)
{
	struct device_node* sub_node;
	
	for (; node != 0;node = node->sibling) {
		unsigned int *class_code, *reg;
		
		reg = (unsigned int *) get_property(node, "reg", 0);
		if (reg && ((reg[0] >> 8) & 0xff) == dev_fn
			&& ((reg[0] >> 16) & 0xff) == bus)
			return node;

		/* For PCI<->PCI bridges or CardBus bridges, we go down
		 * Note: some OFs create a parent node "multifunc-device" as
		 * a fake root for all functions of a multi-function device,
		 * we go down them as well.
		 */
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if ((!class_code || ((*class_code >> 8) != PCI_CLASS_BRIDGE_PCI &&
			(*class_code >> 8) != PCI_CLASS_BRIDGE_CARDBUS)) &&
			strcmp(node->name, "multifunc-device"))
			continue;
		sub_node = scan_OF_childs_for_device(node->child, bus, dev_fn);
		if (sub_node)
			return sub_node;
	}
	return NULL;
}

/* 
 * Scans the OF tree for a device node matching a PCI device
 */
struct device_node*
pci_device_to_OF_node(struct pci_dev *dev)
{
	struct pci_controller *hose;
	struct device_node *node;
	int bus;
	
	if (!have_of)
		return NULL;
		
	/* Lookup the hose */
	bus = dev->bus->number;
	hose = pci_bus_to_hose(bus);
	if (!hose)
		return NULL;

	/* Check it has an OF node associated */
	node = (struct device_node *) hose->arch_data;
	if (!node)
		return NULL;

	/* Fixup bus number according to what OF think it is. */
	if (pci_to_OF_bus_map)
		bus = pci_to_OF_bus_map[bus];
	if (bus == 0xff)
		return NULL;
		
	/* Now, lookup childs of the hose */
	return scan_OF_childs_for_device(node->child, bus, dev->devfn);
}

/* This routine is meant to be used early during boot, when the
 * PCI bus numbers have not yet been assigned, and you need to
 * issue PCI config cycles to an OF device.
 * It could also be used to "fix" RTAS config cycles if you want
 * to set pci_assign_all_busses to 1 and still use RTAS for PCI
 * config cycles.
 */
struct pci_controller*
pci_find_hose_for_OF_device(struct device_node* node)
{
	if (!have_of)
		return NULL;
	while(node) {
		struct pci_controller* hose;
		for (hose=hose_head;hose;hose=hose->next)
			if (hose->arch_data == node)
				return hose;
		node=node->parent;
	}
	return NULL;
}

/* 
 * Returns the PCI device matching a given OF node
 */
int
pci_device_from_OF_node(struct device_node* node, u8* bus, u8* devfn)
{
	unsigned int *reg;
	int i;
		
	if (!have_of)
		return -ENODEV;
	reg = (unsigned int *) get_property(node, "reg", 0);
	if (!reg)
		return -ENODEV;
	*bus = (reg[0] >> 16) & 0xff;
	for (i=0; pci_to_OF_bus_map && i<pci_bus_count; i++)
		if (pci_to_OF_bus_map[i] == *bus) {
			*bus = i;
			break;
		}
	*devfn = ((reg[0] >> 8) & 0xff);
	return 0;
}

void __init
pci_process_bridge_OF_ranges(struct pci_controller *hose,
			   struct device_node *dev, int primary)
{
	unsigned int *ranges, *prev;
	int rlen = 0;
	int memno = 0;
	struct resource *res;
	int np, na = prom_n_addr_cells(dev);
	np = na + 5;

	/* First we try to merge ranges to fix a problem with some pmacs
	 * that can have more than 3 ranges, fortunately using contiguous
	 * addresses -- BenH
	 */
	ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	prev = NULL;
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		if (prev) {
			if (prev[0] == ranges[0] && prev[1] == ranges[1] &&
				(prev[2] + prev[na+4]) == ranges[2] &&
				(prev[na+2] + prev[na+4]) == ranges[na+2]) {
				prev[na+4] += ranges[na+4];
				ranges[0] = 0;
				ranges += np;
				continue;
			}
		}
		prev = ranges;
		ranges += np;
	}

	/*
	 * The ranges property is laid out as an array of elements,
	 * each of which comprises:
	 *   cells 0 - 2:	a PCI address
	 *   cells 3 or 3+4:	a CPU physical address
	 *			(size depending on dev->n_addr_cells)
	 *   cells 4+5 or 5+6:	the size of the range
	 */
	rlen = 0;
	hose->io_base_phys = 0;
	ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		res = NULL;
		switch (ranges[0] >> 24) {
		case 1:		/* I/O space */
			if (ranges[2] != 0)
				break;
			hose->io_base_phys = ranges[na+2];
			hose->io_base_virt = ioremap(ranges[na+2], ranges[na+4]);
			if (primary)
				isa_io_base = (unsigned long) hose->io_base_virt;
			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = ranges[2];
			break;
		case 2:		/* memory space */
			memno = 0;
			if (ranges[1] == 0 && ranges[2] == 0
			    && ranges[na+4] <= (16 << 20)) {
				/* 1st 16MB, i.e. ISA memory area */
				if (primary)
					isa_mem_base = ranges[na+2];
				memno = 1;
			}
			while (memno < 3 && hose->mem_resources[memno].flags)
				++memno;
			if (memno == 0)
				hose->pci_mem_offset = ranges[na+2] - ranges[2];
			if (memno < 3) {
				res = &hose->mem_resources[memno];
				res->flags = IORESOURCE_MEM;
				res->start = ranges[na+2];
			}
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + ranges[na+4] - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
		ranges += np;
	}
}
#endif /* CONFIG_ALL_PPC */

void __init
pcibios_init(void)
{
	struct pci_controller *hose;
	struct pci_bus *bus;
	int next_busno;

	printk(KERN_INFO "PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {
		if (pci_assign_all_busses)
			hose->first_busno = next_busno;
		hose->last_busno = 0xff;
		bus = pci_scan_bus(hose->first_busno, hose->ops, hose);
		hose->last_busno = bus->subordinate;
		if (pci_assign_all_busses || next_busno <= hose->last_busno)
			next_busno = hose->last_busno+1;
	}
	pci_bus_count = next_busno;

	/* OpenFirmware based machines need a map of OF bus
	 * numbers vs. kernel bus numbers since we may have to
	 * remap them.
	 */
	if (pci_assign_all_busses && have_of)
		pcibios_make_OF_bus_map();

	/* Call machine dependant fixup */
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();

	/* Allocate and assign resources */
	pcibios_allocate_bus_resources(&pci_root_buses);
	pcibios_allocate_resources(0);
	pcibios_allocate_resources(1);
	pcibios_assign_resources();

	/* Call machine dependent post-init code */
	if (ppc_md.pcibios_after_init)
		ppc_md.pcibios_after_init();
}

int __init
pcibios_assign_all_busses(void)
{
	return pci_assign_all_busses;
}

void __init
pcibios_fixup_pbus_ranges(struct pci_bus * bus, struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

unsigned long resource_fixup(struct pci_dev * dev, struct resource * res,
			     unsigned long start, unsigned long size)
{
	return start;
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = (struct pci_controller *) bus->sysdata;
	unsigned long io_offset;
	struct resource *res;
	int i;

	io_offset = (unsigned long)hose->io_base_virt - isa_io_base;
	if (bus->parent == NULL) {
		/* This is a host bridge - fill in its resources */
		hose->bus = bus;

		bus->resource[0] = res = &hose->io_resource;
		if (!res->flags) {
			if (io_offset)
				printk(KERN_ERR "I/O resource not set for host"
				       " bridge %d\n", hose->index);
			res->start = 0;
			res->end = IO_SPACE_LIMIT;
			res->flags = IORESOURCE_IO;
		}
		res->start += io_offset;
		res->end += io_offset;

		for (i = 0; i < 3; ++i) {
			res = &hose->mem_resources[i];
			if (!res->flags) {
				if (i > 0)
					continue;
				printk(KERN_ERR "Memory resource not set for "
				       "host bridge %d\n", hose->index);
				res->start = hose->pci_mem_offset;
				res->end = ~0U;
				res->flags = IORESOURCE_MEM;
			}
			bus->resource[i+1] = res;
		}
	} else {
		/* This is a subordinate bridge */
		pci_read_bridge_bases(bus);

		for (i = 0; i < 4; ++i) {
			if ((res = bus->resource[i]) == NULL)
				continue;
			if (!res->flags)
				continue;
			if (io_offset && (res->flags & IORESOURCE_IO)) {
				res->start += io_offset;
				res->end += io_offset;
			} else if (hose->pci_mem_offset
				   && (res->flags & IORESOURCE_MEM)) {
				res->start += hose->pci_mem_offset;
				res->end += hose->pci_mem_offset;
			}
		}
	}

	if (ppc_md.pcibios_fixup_bus)
		ppc_md.pcibios_fixup_bus(bus);
}

char __init *pcibios_setup(char *str)
{
	return str;
}

/* the next one is stolen from the alpha port... */
void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
	/* XXX FIXME - update OF device tree node interrupt property */
}

int pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if (ppc_md.pcibios_enable_device_hook)
		if (ppc_md.pcibios_enable_device_hook(dev, 0))
			return -EINVAL;
			
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because of resource collisions\n", dev->slot_name);
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       dev->slot_name, old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

struct pci_controller*
pci_bus_to_hose(int bus)
{
	struct pci_controller* hose = hose_head;

	for (; hose; hose = hose->next)
		if (bus >= hose->first_busno && bus <= hose->last_busno)
			return hose;
	return NULL;
}

void*
pci_bus_io_base(unsigned int bus)
{
	struct pci_controller *hose;

	hose = pci_bus_to_hose(bus);
	if (!hose)
		return NULL;
	return hose->io_base_virt;
}

unsigned long
pci_bus_io_base_phys(unsigned int bus)
{
	struct pci_controller *hose;

	hose = pci_bus_to_hose(bus);
	if (!hose)
		return 0;
	return hose->io_base_phys;
}

unsigned long
pci_bus_mem_base_phys(unsigned int bus)
{
	struct pci_controller *hose;

	hose = pci_bus_to_hose(bus);
	if (!hose)
		return 0;
	return hose->pci_mem_offset;
}

unsigned long
pci_resource_to_bus(struct pci_dev *pdev, struct resource *res)
{
	/* Hack alert again ! See comments in chrp_pci.c
	 */
	struct pci_controller* hose =
		(struct pci_controller *)pdev->sysdata;
	if (hose && res->flags & IORESOURCE_MEM)
		return res->start - hose->pci_mem_offset;
	/* We may want to do something with IOs here... */
	return res->start;
}

/*
 * Return the index of the PCI controller for device pdev.
 */
int pci_controller_num(struct pci_dev *dev)
{
	struct pci_controller *hose = (struct pci_controller *) dev->sysdata;

	return hose->index;
}

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s,
 * modelled on the sparc64 implementation by Dave Miller.
 *  -- paulus.
 */

/*
 * Adjust vm_pgoff of VMA such that it is the physical page offset
 * corresponding to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static __inline__ int
__pci_mmap_make_offset(struct pci_dev *dev, struct vm_area_struct *vma,
		       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = (struct pci_controller *) dev->sysdata;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long io_offset = 0;
	int i, res_bit;

	if (hose == 0)
		return -EINVAL;		/* should never happen */

	/* If memory, add on the PCI bridge address offset */
	if (mmap_state == pci_mmap_mem) {
		offset += hose->pci_mem_offset;
		res_bit = IORESOURCE_MEM;
	} else {
		io_offset = (unsigned long)hose->io_base_virt - isa_io_base;
		offset += io_offset;
		res_bit = IORESOURCE_IO;
	}

	/*
	 * Check that the offset requested corresponds to one of the
	 * resources of the device.
	 */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &dev->resource[i];
		int flags = rp->flags;

		/* treat ROM as memory (should be already) */
		if (i == PCI_ROM_RESOURCE)
			flags |= IORESOURCE_MEM;

		/* Active and same type? */
		if ((flags & res_bit) == 0)
			continue;

		/* In the range of this resource? */
		if (offset < (rp->start & PAGE_MASK) || offset > rp->end)
			continue;

		/* found it! construct the final physical address */
		if (mmap_state == pci_mmap_io)
			offset += hose->io_base_phys - io_offset;

		vma->vm_pgoff = offset >> PAGE_SHIFT;
		return 0;
	}

	return -EINVAL;
}

/*
 * Set vm_flags of VMA, as appropriate for this architecture, for a pci device
 * mapping.
 */
static __inline__ void
__pci_mmap_set_flags(struct pci_dev *dev, struct vm_area_struct *vma,
		     enum pci_mmap_state mmap_state)
{
	vma->vm_flags |= VM_SHM | VM_LOCKED | VM_IO;
}

/*
 * Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static __inline__ void
__pci_mmap_set_pgprot(struct pci_dev *dev, struct vm_area_struct *vma,
		      enum pci_mmap_state mmap_state, int write_combine)
{
	int prot = pgprot_val(vma->vm_page_prot);

	/* XXX would be nice to have a way to ask for write-through */
	prot |= _PAGE_NO_CACHE;
	if (!write_combine)
		prot |= _PAGE_GUARDED;
	vma->vm_page_prot = __pgprot(prot);
}

/*
 * Perform the actual remap of the pages for a PCI device mapping, as
 * appropriate for this architecture.  The region in the process to map
 * is described by vm_start and vm_end members of VMA, the base physical
 * address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	__pci_mmap_set_flags(dev, vma, mmap_state);
	__pci_mmap_set_pgprot(dev, vma, mmap_state, write_combine);

	ret = remap_page_range(vma->vm_start, vma->vm_pgoff << PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);

	return ret;
}

/* Obsolete functions. Should be removed once the symbios driver
 * is fixed
 */
unsigned long
phys_to_bus(unsigned long pa)
{
	struct pci_controller *hose;
	int i;

	for (hose = hose_head; hose; hose = hose->next) {
		for (i = 0; i < 3; ++i) {
			if (pa >= hose->mem_resources[i].start
			    && pa <= hose->mem_resources[i].end) {
				/*
				 * XXX the hose->pci_mem_offset really
				 * only applies to mem_resources[0].
				 * We need a way to store an offset for
				 * the others.  -- paulus
				 */
				if (i == 0)
					pa -= hose->pci_mem_offset;
				return pa;
			}
		}
	}
	/* hmmm, didn't find it */
	return 0;
}

unsigned long
pci_phys_to_bus(unsigned long pa, int busnr)
{
	struct pci_controller* hose = pci_bus_to_hose(busnr);
	if (!hose)
		return pa;
	return pa - hose->pci_mem_offset;
}

unsigned long
pci_bus_to_phys(unsigned int ba, int busnr)
{
	struct pci_controller* hose = pci_bus_to_hose(busnr);
	if (!hose)
		return ba;
	return ba + hose->pci_mem_offset;
}

/* Provide information on locations of various I/O regions in physical
 * memory.  Do this on a per-card basis so that we choose the right
 * root bridge.
 * Note that the returned IO or memory base is a physical address
 */

long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn)
{
	struct pci_controller* hose = pci_bus_to_hose(bus);
	long result = -EOPNOTSUPP;

	if (!hose)
		return -ENODEV;
	
	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->pci_mem_offset;
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return (long)isa_mem_base;
	}

	return result;
}

/*
 * Null PCI config access functions, for the case when we can't
 * find a hose.
 */
#define NULL_PCI_OP(rw, size, type)					\
static int								\
null_##rw##_config_##size(struct pci_dev *dev, int offset, type val)	\
{									\
	return PCIBIOS_DEVICE_NOT_FOUND;    				\
}

NULL_PCI_OP(read, byte, u8 *)
NULL_PCI_OP(read, word, u16 *)
NULL_PCI_OP(read, dword, u32 *)
NULL_PCI_OP(write, byte, u8)
NULL_PCI_OP(write, word, u16)
NULL_PCI_OP(write, dword, u32)

static struct pci_ops null_pci_ops =
{
	null_read_config_byte,
	null_read_config_word,
	null_read_config_dword,
	null_write_config_byte,
	null_write_config_word,
	null_write_config_dword
};

/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_dev *
fake_pci_dev(struct pci_controller *hose, int busnr, int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	if (hose == 0) {
		hose = pci_bus_to_hose(busnr);
		if (hose == 0)
			printk(KERN_ERR "Can't find hose for PCI bus %d!\n", busnr);
	}
	dev.bus = &bus;
	dev.sysdata = hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.ops = hose? hose->ops: &null_pci_ops;
	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)					\
int early_##rw##_config_##size(struct pci_controller *hose, int bus,	\
			       int devfn, int offset, type value)	\
{									\
	return pci_##rw##_config_##size(fake_pci_dev(hose, bus, devfn),	\
					offset, value);			\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)
