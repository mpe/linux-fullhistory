/*
 * $Id: pci.c,v 1.64 1999/09/17 18:01:53 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>
#include <linux/errno.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>

#include "pci.h"

static void __init pcibios_claim_resources(struct list_head *);

unsigned long isa_io_base     = 0;
unsigned long isa_mem_base    = 0;
unsigned long pci_dram_offset = 0;

struct pci_fixup pcibios_fixups[] = {
	{ 0 }
};

int generic_pcibios_read_byte(struct pci_dev *dev, int where, u8 *val)
{
	return ppc_md.pcibios_read_config_byte(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_read_word(struct pci_dev *dev, int where, u16 *val)
{
	return ppc_md.pcibios_read_config_word(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_read_dword(struct pci_dev *dev, int where, u32 *val)
{
	return ppc_md.pcibios_read_config_dword(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_byte(struct pci_dev *dev, int where, u8 val)
{
	return ppc_md.pcibios_write_config_byte(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_word(struct pci_dev *dev, int where, u16 val)
{
	return ppc_md.pcibios_write_config_word(dev->bus->number,dev->devfn,where,val);
}
int generic_pcibios_write_dword(struct pci_dev *dev, int where, u32 val)
{
	return ppc_md.pcibios_write_config_dword(dev->bus->number,dev->devfn,where,val);
}

struct pci_ops generic_pci_ops = 
{
	generic_pcibios_read_byte,
	generic_pcibios_read_word,
	generic_pcibios_read_dword,
	generic_pcibios_write_byte,
	generic_pcibios_write_word,
	generic_pcibios_write_dword
};

void __init pcibios_init(void)
{
	printk("PCI: Probing PCI hardware\n");
	pci_scan_bus(0, &generic_pci_ops, NULL);
	pcibios_claim_resources(&pci_root_buses);
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();
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

static void __init pcibios_claim_resources(struct list_head *bus_list)
{
	struct list_head *ln, *dn;
	struct pci_bus *bus;
	struct pci_dev *dev;
	int idx;

	for (ln=bus_list->next; ln != bus_list; ln=ln->next) {
		bus = pci_bus_b(ln);
		for (dn=bus->devices.next; dn != &bus->devices; dn=dn->next) {
			dev = pci_dev_b(dn);
			for (idx = 0; idx < PCI_NUM_RESOURCES; idx++)
			{
				struct resource *r = &dev->resource[idx];
				struct resource *pr;
				if (!r->start)
					continue;
				pr = pci_find_parent_resource(dev, r);
				if (!pr || request_resource(pr, r) < 0)
				{
					printk(KERN_ERR "PCI: Address space collision on region %d of device %s\n", idx, dev->name);
					/* We probably should disable the region, shouldn't we? */
				}
			}
		}
		pcibios_claim_resources(&bus->children);
	}
}

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	if ( ppc_md.pcibios_fixup_bus )
		ppc_md.pcibios_fixup_bus(bus);
}

char __init *pcibios_setup(char *str)
{
	return str;
}

int pcibios_assign_resource(struct pci_dev *pdev, int resource)
{
	return 0;
}

/* the next two are stolen from the alpha port... */
void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
        unsigned long where, size;
        u32 reg;

        where = PCI_BASE_ADDRESS_0 + (resource * 4);
        size = res->end - res->start;
        pci_read_config_dword(dev, where, &reg);
        reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
        pci_write_config_dword(dev, where, reg);
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
	/* XXX FIXME - update OF device tree node interrupt property */
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

int pcibios_enable_device(struct pci_dev *dev)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

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

/*
 * Assign new address to PCI resource.  We hope our resource information
 * is complete.  We don't re-assign resources unless we are
 * forced to do so.
 *
 * Expects start=0, end=size-1, flags=resource type.
 */

int pci_assign_resource(struct pci_dev *dev, int i)
{
	struct resource *r = &dev->resource[i];
	struct resource *pr = pci_find_parent_resource(dev, r);
	unsigned long size = r->end + 1;
	u32 new, check;

	if (!pr) {
		printk(KERN_ERR "PCI: Cannot find parent resource for device %s\n", dev->slot_name);
		return -EINVAL;
	}
	if (r->flags & IORESOURCE_IO) {
		if (allocate_resource(pr, r, size, 0x100, ~0, size, NULL, NULL)) {
			printk(KERN_ERR "PCI: Allocation of I/O region %s/%d (%ld bytes) failed\n", dev->slot_name, i, size);
			return -EBUSY;
		}
	} else {
		if (allocate_resource(pr, r, size, 0x10000, ~0, size, NULL, NULL)) {
			printk(KERN_ERR "PCI: Allocation of memory region %s/%d (%ld bytes) failed\n", dev->slot_name, i, size);
			return -EBUSY;
		}
	}
	if (i < 6) {
		int reg = PCI_BASE_ADDRESS_0 + 4*i;
		new = r->start | (r->flags & PCI_REGION_FLAG_MASK);
		pci_write_config_dword(dev, reg, new);
		pci_read_config_dword(dev, reg, &check);
		if (new != check)
			printk(KERN_ERR "PCI: Error while updating region %s/%d (%08x != %08x)\n", dev->slot_name, i, new, check);
	} else if (i == PCI_ROM_RESOURCE) {
		r->flags |= PCI_ROM_ADDRESS_ENABLE;
		pci_write_config_dword(dev, dev->rom_base_reg, r->start | (r->flags & PCI_REGION_FLAG_MASK));
	}
	printk("PCI: Assigned addresses %08lx-%08lx to region %s/%d\n", r->start, r->end, dev->slot_name, i);
	return 0;
}
