/* $Id: ip27-pci.c,v 1.8 2000/02/16 01:07:30 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/sn/arch.h>
#include <asm/pci/bridge.h>
#include <asm/paccess.h>

/*
 * The Bridge ASIC supports both type 0 and type 1 access.  Type 1 is
 * not really documented, so right now I can't write code which uses it.
 * Therefore we use type 0 accesses for now even though they won't work
 * correcly for PCI-to-PCI bridges.
 */
#define CF0_READ_PCI_CFG(dev,where,value,bm,mask)			\
do {									\
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;		\
	int slot = PCI_SLOT(dev->devfn);				\
	int fn = PCI_FUNC(dev->devfn);					\
	volatile u32 *addr;						\
	u32 cf, __bit;							\
									\
	if (dev->bus->number)						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
									\
	__bit = (((where) & (bm)) << 3);				\
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];	\
	if (get_dbe(cf, addr))						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
	*value = (cf >> __bit) & (mask);				\
	return PCIBIOS_SUCCESSFUL;					\
} while (0)

static int
pci_conf0_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,3,0xff);
}

static int
pci_conf0_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,2,0xffff);
}

static int
pci_conf0_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	CF0_READ_PCI_CFG(dev,where,value,0,0xffffffff);
}

#define CF0_WRITE_PCI_CFG(dev,where,value,bm,mask)			\
do {									\
	bridge_t *bridge = (bridge_t *) 0x9200000008000000;		\
	int slot = PCI_SLOT(dev->devfn);				\
	int fn = PCI_FUNC(dev->devfn);					\
	volatile u32 *addr;						\
	u32 cf, __bit;							\
									\
	if (dev->bus->number)						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
									\
	if (dev->vendor == PCI_VENDOR_ID_SGI				\
	    && dev->device == PCI_DEVICE_ID_SGI_IOC3)			\
		return PCIBIOS_SUCCESSFUL;				\
									\
	__bit = (((where) & (bm)) << 3);				\
	addr = &bridge->b_type0_cfg_dev[slot].f[fn].l[where >> 2];	\
	if (get_dbe(cf, addr))						\
		return PCIBIOS_DEVICE_NOT_FOUND;			\
	cf &= (~mask);							\
	cf |= (value);							\
	put_dbe(cf, addr);						\
	return PCIBIOS_SUCCESSFUL;					\
} while (0)

static int
pci_conf0_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,3,0xff);
}

static int
pci_conf0_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,2,0xffff);
}

static int
pci_conf0_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	CF0_WRITE_PCI_CFG(dev,where,value,0,0xffffffff);
}


static struct pci_ops bridge_pci_ops = {
	pci_conf0_read_config_byte,
	pci_conf0_read_config_word,
	pci_conf0_read_config_dword,
	pci_conf0_write_config_byte,
	pci_conf0_write_config_word,
	pci_conf0_write_config_dword
};

void __init pcibios_init(void)
{
	struct pci_ops *ops = &bridge_pci_ops;
	nasid_t nid = get_nasid();

	ioport_resource.end = ~0UL;

	printk("PCI: Probing PCI hardware on host bus 0, node %d.\n", nid);
	pci_scan_bus(0, ops, NULL);
}

static inline u8
bridge_swizzle(u8 pin, u8 slot) 
{
	return (((pin-1) + slot) % 4) + 1;
}

static u8 __init
pci_swizzle(struct pci_dev *dev, u8 *pinp)
{
	u8 pin = *pinp;

	while (dev->bus->self) {	/* Move up the chain of bridges. */
		pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		dev = dev->bus->self;
	}
	*pinp = pin;

	return PCI_SLOT(dev->devfn);
}

/* XXX This should include the node ID into the final interrupt number.  */
static int __init
pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return (slot + (((pin-1) & 1) << 2)) & 7;
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

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
pcibios_fixup_bus(struct pci_bus *b)
{
	unsigned short command;
	struct list_head *ln;
	struct pci_dev *dev;

	pci_fixup_irqs(pci_swizzle, pci_map_irq);

	/*
	 * Older qlogicisp driver expects to have the IO space enable 
	 * bit set. Make that happen for qlogic in slots 0 and 1. Things
	 * stop working if we program the controllers as not having
	 * PCI_COMMAND_MEMORY, so we have to fudge the mem_flags.
	 */
	for (ln=b->devices.next; ln != &b->devices; ln=ln->next) {
		dev = pci_dev_b(ln);
		if (PCI_FUNC(dev->devfn) == 0) {
			if ((PCI_SLOT(dev->devfn) == 0) || 
						(PCI_SLOT(dev->devfn) == 1)) {
				if (pci_read_config_word(dev, PCI_COMMAND, 
								&command) == 0) {
					command |= PCI_COMMAND_IO;
					pci_write_config_word(dev, PCI_COMMAND, 
									command);
					dev->resource[1].flags |= 1;
				}
			}
		}
	}
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

int __init
pcibios_enable_device(struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

void __init
pcibios_align_resource(void *data, struct resource *res, unsigned long size)
{
}

char * __init
pcibios_setup(char *str)
{
	/* Nothing to do for now.  */

	return str;
}

static void __init
pci_fixup_ioc3(struct pci_dev *d)
{
	int i;

	/* IOC3 only decodes 0x20 bytes of the config space, so we end up
	   with tons of bogus information in the pci_dev.  On Origins the
	   INTA, INTB and INTC pins are all wired together as if it'd only
	   use INTA.  */
	printk("PCI: Fixing base addresses for device %s\n", d->slot_name);

	for (i = 1; i <= PCI_ROM_RESOURCE; i++) {
		d->resource[i].start = 0UL;
		d->resource[i].end = 0UL;
		d->resource[i].flags = 0UL;
	}
	d->subsystem_vendor = 0;
	d->subsystem_device = 0;
	d->irq = 1;
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3,
	  pci_fixup_ioc3 },
	{ 0 }
};
