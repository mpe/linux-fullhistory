/*
 * Support for PCI bridges found on Power Macintoshes.
 * At present the "bandit" and "chaos" bridges are supported.
 * Fortunately you access configuration space in the same
 * way with either bridge.
 *
 * Copyright (C) 1997 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#include "pci.h"

struct bridge_data **bridges, *bridge_list;
static int max_bus;

static void add_bridges(struct device_node *dev);

/*
 * Magic constants for enabling cache coherency in the bandit/PSX bridge.
 */
#define APPLE_VENDID	0x106b
#define BANDIT_DEVID	1
#define BANDIT_DEVID_2	8
#define BANDIT_REVID	3

#define BANDIT_DEVNUM	11
#define BANDIT_MAGIC	0x50
#define BANDIT_COHERENT	0x40

__pmac
void *pci_io_base(unsigned int bus)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return 0;
	return bp->io_base;
}

__pmac
int pci_device_loc(struct device_node *dev, unsigned char *bus_ptr,
		   unsigned char *devfn_ptr)
{
	unsigned int *reg;
	int len;

	reg = (unsigned int *) get_property(dev, "reg", &len);
	if (reg == 0 || len < 5 * sizeof(unsigned int)) {
		/* doesn't look like a PCI device */
		*bus_ptr = 0xff;
		*devfn_ptr = 0xff;
		return -1;
	}
	*bus_ptr = reg[0] >> 16;
	*devfn_ptr = reg[0] >> 8;
	return 0;
}

__pmac
int pmac_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char *val)
{
	struct bridge_data *bp;

	*val = 0xff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* Bus number once again taken into consideration.
		 * Change applied from 2.1.24. This makes devices located
		 * behind PCI-PCI bridges visible.
		 * -Ranjit Deshpande, 01/20/99
		 */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_8(bp->cfg_data + (offset & 3));
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short *val)
{
	struct bridge_data *bp;

	*val = 0xffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)));
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int *val)
{
	struct bridge_data *bp;

	*val = 0xffffffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + offset + 1);
	}
	udelay(2);
	*val = in_le32((volatile unsigned int *)bp->cfg_data);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned char val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_8(bp->cfg_data + (offset & 3), val);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned short val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)), val);
	return PCIBIOS_SUCCESSFUL;
}

__pmac
int pmac_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		/* See pci_read_config_byte */
		out_le32(bp->cfg_addr, (bus << 16) + (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	return PCIBIOS_SUCCESSFUL;
}

#define GRACKLE_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))

int grackle_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char *val)
{
	struct bridge_data *bp;

	*val = 0xff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_8(bp->cfg_data + (offset & 3));
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short *val)
{
	struct bridge_data *bp;

	*val = 0xffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_le16((volatile unsigned short *)(bp->cfg_data + (offset&3)));
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int *val)
{
	struct bridge_data *bp;

	*val = 0xffffffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 3) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	*val = in_le32((volatile unsigned int *)bp->cfg_data);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned char val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_8(bp->cfg_data + (offset & 3), val);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned short val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_le16((volatile unsigned short *)(bp->cfg_data + (offset&3)), val);
	return PCIBIOS_SUCCESSFUL;
}

int grackle_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned int val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((offset & 1) != 0)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	out_be32(bp->cfg_addr, GRACKLE_CFA(bus, dev_fn, offset));
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	return PCIBIOS_SUCCESSFUL;
}

/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we can't use pcibios_*_config_* here because bridges[]
 * is not initialized yet.
 */
static void __init init_bandit(struct bridge_data *bp)
{
	unsigned int vendev, magic;
	int rev;

	/* read the word at offset 0 in config space for device 11 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_VENDOR_ID);
	udelay(2);
	vendev = in_le32((volatile unsigned int *)bp->cfg_data);
	if (vendev == (BANDIT_DEVID << 16) + APPLE_VENDID) {
		/* read the revision id */
		out_le32(bp->cfg_addr,
			 (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
		udelay(2);
		rev = in_8(bp->cfg_data);
		if (rev != BANDIT_REVID)
			printk(KERN_WARNING
			       "Unknown revision %d for bandit at %p\n",
			       rev, bp->io_base);
	} else if (vendev != (BANDIT_DEVID_2 << 16) + APPLE_VENDID) {
		printk(KERN_WARNING "bandit isn't? (%x)\n", vendev);
		return;
	}

	/* read the revision id */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_REVISION_ID);
	udelay(2);
	rev = in_8(bp->cfg_data);
	if (rev != BANDIT_REVID)
		printk(KERN_WARNING "Unknown revision %d for bandit at %p\n",
		       rev, bp->io_base);

	/* read the word at offset 0x50 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + BANDIT_MAGIC);
	udelay(2);
	magic = in_le32((volatile unsigned int *)bp->cfg_data);
	if ((magic & BANDIT_COHERENT) != 0)
		return;
	magic |= BANDIT_COHERENT;
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, magic);
	printk(KERN_INFO "Cache coherency enabled for bandit/PSX at %p\n",
	       bp->io_base);
}

void __init pmac_find_bridges(void)
{
	int bus;
	struct bridge_data *bridge;

	bridge_list = 0;
	max_bus = 0;
	add_bridges(find_devices("bandit"));
	add_bridges(find_devices("chaos"));
	add_bridges(find_devices("pci"));
	bridges = (struct bridge_data **)
		alloc_bootmem((max_bus + 1) * sizeof(struct bridge_data *));
	memset(bridges, 0, (max_bus + 1) * sizeof(struct bridge_data *));
	for (bridge = bridge_list; bridge != NULL; bridge = bridge->next)
		for (bus = bridge->bus_number; bus <= bridge->max_bus; ++bus)
			bridges[bus] = bridge;
}

/*
 * We assume that if we have a G3 powermac, we have one bridge called
 * "pci" (a MPC106) and no bandit or chaos bridges, and contrariwise,
 * if we have one or more bandit or chaos bridges, we don't have a MPC106.
 */
static void __init add_bridges(struct device_node *dev)
{
	int *bus_range;
	int len;
	struct bridge_data *bp;
	struct reg_property *addr;

	for (; dev != NULL; dev = dev->next) {
		addr = (struct reg_property *) get_property(dev, "reg", &len);
		if (addr == NULL || len < sizeof(*addr)) {
			printk(KERN_WARNING "Can't use %s: no address\n",
			       dev->full_name);
			continue;
		}
		bus_range = (int *) get_property(dev, "bus-range", &len);
		if (bus_range == NULL || len < 2 * sizeof(int)) {
			printk(KERN_WARNING "Can't get bus-range for %s\n",
			       dev->full_name);
			continue;
		}
		if (bus_range[1] == bus_range[0])
			printk(KERN_INFO "PCI bus %d", bus_range[0]);
		else
			printk(KERN_INFO "PCI buses %d..%d", bus_range[0],
			       bus_range[1]);
		printk(" controlled by %s at %x\n", dev->name, addr->address);
		bp = (struct bridge_data *) alloc_bootmem(sizeof(*bp));
		if (strcmp(dev->name, "pci") != 0) {
			bp->cfg_addr = (volatile unsigned int *)
				ioremap(addr->address + 0x800000, 0x1000);
			bp->cfg_data = (volatile unsigned char *)
				ioremap(addr->address + 0xc00000, 0x1000);
			bp->io_base = (void *) ioremap(addr->address, 0x10000);
		} else {
			/* XXX */
			bp->cfg_addr = (volatile unsigned int *)
				ioremap(0xfec00000, 0x1000);
			bp->cfg_data = (volatile unsigned char *)
				ioremap(0xfee00000, 0x1000);
                        bp->io_base = (void *) ioremap(0xfe000000, 0x20000);
		}
		if (isa_io_base == 0)
			isa_io_base = (unsigned long) bp->io_base;
		bp->bus_number = bus_range[0];
		bp->max_bus = bus_range[1];
		bp->next = bridge_list;
		bp->node = dev;
		bridge_list = bp;
		if (bp->max_bus > max_bus)
			max_bus = bp->max_bus;

		if (strcmp(dev->name, "bandit") == 0)
			init_bandit(bp);
	}
}

/* Recursively searches any node that is of type PCI-PCI bridge. Without
 * this, the old code would miss children of P2P bridges and hence not
 * fix IRQ's for cards located behind P2P bridges.
 * - Ranjit Deshpande, 01/20/99
 */
void __init
fix_intr(struct device_node *node, struct pci_dev *dev)
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

void __init
pmac_pcibios_fixup(void)
{
	struct pci_dev *dev;
	
	/*
	 * FIXME: This is broken: We should not assign IRQ's to IRQless
	 *	  devices (look at PCI_INTERRUPT_PIN) and we also should
	 *	  honor the existence of multi-function devices where
	 *	  different functions have different interrupt pins. [mj]
	 */
	pci_for_each_dev(dev)
	{
		/*
		 * Open Firmware often doesn't initialize the,
		 * PCI_INTERRUPT_LINE config register properly, so we
		 * should find the device node and se if it has an
		 * AAPL,interrupts property.
		 */
		struct bridge_data *bp = bridges[dev->bus->number];
		unsigned char pin;
			
		if (pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin) ||
		    !pin)
			continue; /* No interrupt generated -> no fixup */
                fix_intr(bp->node->child, dev);
	}
}

void __init
pmac_setup_pci_ptrs(void)
{
	if (find_devices("pci") != 0) {
		/* looks like a G3 powermac */
		set_config_access_method(grackle);
	} else {
		set_config_access_method(pmac);
	}

	ppc_md.pcibios_fixup = pmac_pcibios_fixup;
}

