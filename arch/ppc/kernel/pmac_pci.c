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
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

struct bridge_data {
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;
	void *io_base;
	int bus_number;
	int max_bus;
	struct bridge_data *next;
	struct device_node *node;
};

static struct bridge_data **bridges, *bridge_list;
static int max_bus;

static void add_bridges(struct device_node *dev, unsigned long *mem_ptr);

/*
 * Magic constants for enabling cache coherency in the bandit/PSX bridge.
 */
#define APPLE_VENDID	0x106b
#define BANDIT_DEVID	1
#define BANDIT_REVID	3

#define BANDIT_DEVNUM	11
#define BANDIT_MAGIC	0x50
#define BANDIT_COHERENT	0x40

/*
 * For a bandit bridge, turn on cache coherency if necessary.
 * N.B. we can't use pcibios_*_config_* here because bridges[]
 * is not initialized yet.
 */
static void init_bandit(struct bridge_data *bp)
{
	unsigned int vendev, magic;
	int rev;

	/* read the word at offset 0 in config space for device 11 */
	out_le32(bp->cfg_addr, (1UL << BANDIT_DEVNUM) + PCI_VENDOR_ID);
	udelay(2);
	vendev = in_le32((volatile unsigned int *)bp->cfg_data);
	if (vendev != (BANDIT_DEVID << 16) + APPLE_VENDID) {
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

unsigned long pmac_find_bridges(unsigned long mem_start, unsigned long mem_end)
{
	int bus;
	struct bridge_data *bridge;

	bridge_list = 0;
	max_bus = 0;
	add_bridges(find_devices("bandit"), &mem_start);
	add_bridges(find_devices("chaos"), &mem_start);
	bridges = (struct bridge_data **) mem_start;
	mem_start += (max_bus + 1) * sizeof(struct bridge_data *);
	memset(bridges, 0, (max_bus + 1) * sizeof(struct bridge_data *));
	for (bridge = bridge_list; bridge != NULL; bridge = bridge->next)
		for (bus = bridge->bus_number; bus <= bridge->max_bus; ++bus)
			bridges[bus] = bridge;

	return mem_start;
}

static void add_bridges(struct device_node *dev, unsigned long *mem_ptr)
{
	int *bus_range;
	int len;
	struct bridge_data *bp;

	for (; dev != NULL; dev = dev->next) {
		if (dev->n_addrs < 1) {
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
		printk(" controlled by %s at %x\n",
		       dev->name, dev->addrs[0].address);
		bp = (struct bridge_data *) *mem_ptr;
		*mem_ptr += sizeof(struct bridge_data);
		bp->cfg_addr = (volatile unsigned int *)
			(dev->addrs[0].address + 0x800000);
		bp->cfg_data = (volatile unsigned char *)
			(dev->addrs[0].address + 0xc00000);
		bp->io_base = (void *) dev->addrs[0].address;
		ioremap(dev->addrs[0].address, 0x800000);
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

void *pci_io_base(unsigned int bus)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0)
		return 0;
	return bp->io_base;
}

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
		out_le32(bp->cfg_addr, (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_8(bp->cfg_data + (offset & 3));

	if (offset == PCI_INTERRUPT_LINE) {
		/*
		 * Open Firmware often doesn't initialize this
		 * register properly, so we find the node and see
		 * if it has an AAPL,interrupts property.
		 */
		struct device_node *node;
		unsigned int *reg;

		for (node = bp->node->child; node != 0; node = node->sibling) {
			reg = (unsigned int *) get_property(node, "reg", 0);
			if (reg == 0 || ((reg[0] >> 8) & 0xff) != dev_fn)
				continue;
			/* this is the node, see if it has interrupts */
			if (node->n_intrs > 0)
				*val = node->intrs[0];
			break;
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

int pmac_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
			     unsigned char offset, unsigned short *val)
{
	struct bridge_data *bp;

	*val = 0xffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0 || (offset & 1) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		out_le32(bp->cfg_addr, (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	*val = in_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)));
	return PCIBIOS_SUCCESSFUL;
}

int pmac_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned int *val)
{
	struct bridge_data *bp;

	*val = 0xffffffff;
	if (bus > max_bus || (bp = bridges[bus]) == 0 || (offset & 3) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		out_le32(bp->cfg_addr, (dev_fn << 8) + offset + 1);
	}
	udelay(2);
	*val = in_le32((volatile unsigned int *)bp->cfg_data);
	return PCIBIOS_SUCCESSFUL;
}

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
		out_le32(bp->cfg_addr, (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_8(bp->cfg_data + (offset & 3), val);
	return PCIBIOS_SUCCESSFUL;
}

int pmac_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
			      unsigned char offset, unsigned short val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0 || (offset & 1) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + (offset & ~3));
	} else {
		out_le32(bp->cfg_addr, (dev_fn << 8) + (offset & ~3) + 1);
	}
	udelay(2);
	out_le16((volatile unsigned short *)(bp->cfg_data + (offset & 3)), val);
	return PCIBIOS_SUCCESSFUL;
}

int pmac_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
			       unsigned char offset, unsigned int val)
{
	struct bridge_data *bp;

	if (bus > max_bus || (bp = bridges[bus]) == 0 || (offset & 3) != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (bus == bp->bus_number) {
		if (dev_fn < (11 << 3))
			return PCIBIOS_DEVICE_NOT_FOUND;
		out_le32(bp->cfg_addr,
			 (1UL << (dev_fn >> 3)) + ((dev_fn & 7) << 8)
			 + offset);
	} else {
		out_le32(bp->cfg_addr, (dev_fn << 8) + offset + 1);
	}
	udelay(2);
	out_le32((volatile unsigned int *)bp->cfg_data, val);
	return PCIBIOS_SUCCESSFUL;
}

int pmac_pcibios_find_device(unsigned short vendor, unsigned short dev_id,
			unsigned short index, unsigned char *bus_ptr,
			unsigned char *dev_fn_ptr)
{
	int bus, unit, fn, num, devfn;
	unsigned int x, vendev;
	unsigned char h;

	if (vendor == 0xffff)
		return PCIBIOS_BAD_VENDOR_ID;
	vendev = (dev_id << 16) + vendor;
	num = 0;
	for (bus = 0; bus <= max_bus; ++bus) {
		if (bridges[bus] == 0)
			continue;
		unit = fn = 0;
		if (bus == bridges[bus]->bus_number)
			unit = 11;
		while (unit < 32) {
			devfn = PCI_DEVFN(unit, fn);
			if (pcibios_read_config_dword(bus, devfn,
						      PCI_VENDOR_ID, &x)
			    == PCIBIOS_SUCCESSFUL && x == vendev) {
				if (index == num) {
					*bus_ptr = bus;
					*dev_fn_ptr = devfn;
					return PCIBIOS_SUCCESSFUL;
				}
				++num;
			}
			if (fn != 0) {
				if (++fn >= 8) {
					++unit;
					fn = 0;
				}
				continue;
			}
			if (pcibios_read_config_byte(bus, devfn,
						     PCI_HEADER_TYPE, &h)
			    == PCIBIOS_SUCCESSFUL && (h & 0x80) != 0)
				++fn;
			else
				++unit;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}

int pmac_pcibios_find_class(unsigned int class_code, unsigned short index,
		       unsigned char *bus_ptr, unsigned char *dev_fn_ptr)
{
	int bus, unit, fn, num, devfn;
	unsigned int x;
	unsigned char h;

	num = 0;
	for (bus = 0; bus <= max_bus; ++bus) {
		if (bridges[bus] == 0)
			continue;
		unit = fn = 0;
		if (bus == bridges[bus]->bus_number)
			unit = 11;
		while (unit < 32) {
			devfn = PCI_DEVFN(unit, fn);
			if (pcibios_read_config_dword(bus, devfn,
						      PCI_CLASS_REVISION, &x)
			    == PCIBIOS_SUCCESSFUL && (x >> 8) == class_code) {
				if (index == num) {
					*bus_ptr = bus;
					*dev_fn_ptr = devfn;
					return PCIBIOS_SUCCESSFUL;
				}
				++num;
			}
			if (fn != 0) {
				if (++fn >= 8) {
					++unit;
					fn = 0;
				}
				continue;
			}
			if (pcibios_read_config_byte(bus, devfn,
						     PCI_HEADER_TYPE, &h)
			    == PCIBIOS_SUCCESSFUL && (h & 0x80) != 0)
				++fn;
			else
				++unit;
		}
	}
	return PCIBIOS_DEVICE_NOT_FOUND;
}
