/*
 * $Id: pci.c,v 1.64 1999/09/17 18:01:53 cort Exp $
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/openpic.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/gg2.h>

#include "pci.h"

static void __init pcibios_claim_resources(struct pci_bus *);

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
	ioport_resource.end = ~0L;
	pci_scan_bus(0, &generic_pci_ops, NULL);
	pcibios_claim_resources(pci_root);
	if ( ppc_md.pcibios_fixup )
		ppc_md.pcibios_fixup();
}

static void __init pcibios_claim_resources(struct pci_bus *bus)
{
	struct pci_dev *dev;
	int idx;

	while (bus)
	{
		for (dev=bus->devices; dev; dev=dev->sibling)
		{
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
		if (bus->children)
			pcibios_claim_resources(bus->children);
		bus = bus->next;
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

#ifndef CONFIG_8xx
/* Recursively searches any node that is of type PCI-PCI bridge. Without
 * this, the old code would miss children of P2P bridges and hence not
 * fix IRQ's for cards located behind P2P bridges.
 * - Ranjit Deshpande, 01/20/99
 */
void __init fix_intr(struct device_node *node, struct pci_dev *dev)
{
	unsigned int *reg, *class_code;

	for (; node != 0;node = node->sibling) {
		class_code = (unsigned int *) get_property(node, "class-code", 0);
		if((*class_code >> 8) == PCI_CLASS_BRIDGE_PCI)
			fix_intr(node->child, dev);
		reg = (unsigned int *) get_property(node, "reg", 0);
		if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev->devfn)
			continue;
		/* this is the node, see if it has interrupts */
		if (node->n_intrs > 0) 
			dev->irq = node->intrs[0].line;
		break;
	}
}
#endif

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
