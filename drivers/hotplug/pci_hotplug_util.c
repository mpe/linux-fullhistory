/*
 * PCI HotPlug Utility functions
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include "pci_hotplug.h"


#if !defined(CONFIG_HOTPLUG_PCI_MODULE)
	#define MY_NAME	"pci_hotplug"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(fmt, arg...) do { if (debug) printk(KERN_DEBUG "%s: "__FUNCTION__": " fmt , MY_NAME , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format , MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format , MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format , MY_NAME , ## arg)


/* local variables */
static int debug;


static int build_dev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, struct pci_dev **pci_dev)
{
	struct pci_dev *my_dev;
	struct pci_bus *my_bus;

	/* Some validity checks. */
	if ((function > 7) ||
	    (slot > 31) ||
	    (pci_dev == NULL) ||
	    (ops == NULL))
		return -ENODEV;

	my_dev = kmalloc (sizeof (struct pci_dev), GFP_KERNEL);
	if (!my_dev)
		return -ENOMEM;
	my_bus = kmalloc (sizeof (struct pci_bus), GFP_KERNEL);
	if (!my_bus) {
		kfree (my_dev);
		return -ENOMEM;
	}
	memset(my_dev, 0, sizeof(struct pci_dev));
	memset(my_bus, 0, sizeof(struct pci_bus));

	my_bus->number = bus;
	my_bus->ops = ops;
	my_dev->devfn = PCI_DEVFN(slot, function);
	my_dev->bus = my_bus;
	*pci_dev = my_dev;
	return 0;
}

/**
 * pci_read_config_byte_nodev - read a byte from a pci device
 * @ops: pointer to a &struct pci_ops that will be used to read from the pci device
 * @bus: the bus of the pci device to read from
 * @slot: the pci slot number of the pci device to read from
 * @function: the function of the pci device to read from
 * @where: the location on the pci address space to read from
 * @value: pointer to where to place the data read
 *
 * Like pci_read_config_byte() but works for pci devices that do not have a
 * pci_dev structure set up yet.
 * Returns 0 on success.
 */
int pci_read_config_byte_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u8 *value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %p\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_read_config_byte (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_read_config_byte(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/**
 * pci_read_config_word_nodev - read a word from a pci device
 * @ops: pointer to a &struct pci_ops that will be used to read from the pci device
 * @bus: the bus of the pci device to read from
 * @slot: the pci slot number of the pci device to read from
 * @function: the function of the pci device to read from
 * @where: the location on the pci address space to read from
 * @value: pointer to where to place the data read
 *
 * Like pci_read_config_word() but works for pci devices that do not have a
 * pci_dev structure set up yet. 
 * Returns 0 on success.
 */
int pci_read_config_word_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u16 *value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %p\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_read_config_word (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_read_config_word(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/**
 * pci_read_config_dword_nodev - read a dword from a pci device
 * @ops: pointer to a &struct pci_ops that will be used to read from the pci
 * device
 * @bus: the bus of the pci device to read from
 * @slot: the pci slot number of the pci device to read from
 * @function: the function of the pci device to read from
 * @where: the location on the pci address space to read from
 * @value: pointer to where to place the data read
 *
 * Like pci_read_config_dword() but works for pci devices that do not have a
 * pci_dev structure set up yet. 
 * Returns 0 on success.
 */
int pci_read_config_dword_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u32 *value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %p\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_read_config_dword (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_read_config_dword(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/**
 * pci_write_config_byte_nodev - write a byte to a pci device
 * @ops: pointer to a &struct pci_ops that will be used to write to the pci
 * device
 * @bus: the bus of the pci device to write to
 * @slot: the pci slot number of the pci device to write to
 * @function: the function of the pci device to write to
 * @where: the location on the pci address space to write to
 * @value: the value to write to the pci device
 *
 * Like pci_write_config_byte() but works for pci devices that do not have a
 * pci_dev structure set up yet. 
 * Returns 0 on success.
 */
int pci_write_config_byte_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u8 value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %d\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_write_config_byte (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_write_config_byte(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/**
 * pci_write_config_word_nodev - write a word to a pci device
 * @ops: pointer to a &struct pci_ops that will be used to write to the pci
 * device
 * @bus: the bus of the pci device to write to
 * @slot: the pci slot number of the pci device to write to
 * @function: the function of the pci device to write to
 * @where: the location on the pci address space to write to
 * @value: the value to write to the pci device
 *
 * Like pci_write_config_word() but works for pci devices that do not have a
 * pci_dev structure set up yet. 
 * Returns 0 on success.
 */
int pci_write_config_word_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u16 value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %d\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_write_config_word (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_write_config_word(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/**
 * pci_write_config_dword_nodev - write a dword to a pci device
 * @ops: pointer to a &struct pci_ops that will be used to write to the pci
 * device
 * @bus: the bus of the pci device to write to
 * @slot: the pci slot number of the pci device to write to
 * @function: the function of the pci device to write to
 * @where: the location on the pci address space to write to
 * @value: the value to write to the pci device
 *
 * Like pci_write_config_dword() but works for pci devices that do not have a
 * pci_dev structure set up yet. 
 * Returns 0 on success.
 */
int pci_write_config_dword_nodev (struct pci_ops *ops, u8 bus, u8 slot, u8 function, int where, u32 value)
{
	struct pci_dev *dev = NULL;
	int result;

	dbg("%p, %d, %d, %d, %d, %d\n", ops, bus, slot, function, where, value);
	dev = pci_find_slot(bus, PCI_DEVFN(slot, function));
	if (dev) {
		dbg("using native pci_dev\n");
		return pci_write_config_dword (dev, where, value);
	}
	
	result = build_dev (ops, bus, slot, function, &dev);
	if (result)
		return result;
	result = pci_write_config_dword(dev, where, value);
	kfree (dev->bus);
	kfree (dev);
	return result;
}

/*
 * This is code that scans the pci buses.
 * Every bus and every function is presented to a custom
 * function that can act upon it.
 */

static int pci_visit_bus (struct pci_visit * fn, struct pci_bus_wrapped *wrapped_bus, struct pci_dev_wrapped *wrapped_parent)
{
	struct list_head *ln;
	struct pci_dev *dev;
	struct pci_dev_wrapped wrapped_dev;
	int result = 0;

	dbg("scanning bus %02x\n", wrapped_bus->bus->number);

	if (fn->pre_visit_pci_bus) {
		result = fn->pre_visit_pci_bus(wrapped_bus, wrapped_parent);
		if (result)
			return result;
	}

	ln = wrapped_bus->bus->devices.next; 
	while (ln != &wrapped_bus->bus->devices) {
		dev = pci_dev_b(ln);
		ln = ln->next;

		memset(&wrapped_dev, 0, sizeof(struct pci_dev_wrapped));
		wrapped_dev.dev = dev;

		result = pci_visit_dev(fn, &wrapped_dev, wrapped_bus);
		if (result)
			return result;
	}

	if (fn->post_visit_pci_bus)
		result = fn->post_visit_pci_bus(wrapped_bus, wrapped_parent);

	return result;
}


static int pci_visit_bridge (struct pci_visit * fn, struct pci_dev_wrapped *wrapped_dev, struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_bus *bus = wrapped_dev->dev->subordinate;
	struct pci_bus_wrapped wrapped_bus;
	int result;

	memset(&wrapped_bus, 0, sizeof(struct pci_bus_wrapped));
	wrapped_bus.bus = bus;

	dbg("scanning bridge %02x, %02x\n", wrapped_dev->dev->devfn >> 3,
	    wrapped_dev->dev->devfn & 0x7);

	if (fn->visit_pci_dev) {
		result = fn->visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	result = pci_visit_bus(fn, &wrapped_bus, wrapped_dev);
	return result;
}


int pci_visit_dev (struct pci_visit *fn, struct pci_dev_wrapped *wrapped_dev, struct pci_bus_wrapped *wrapped_parent)
{
	struct pci_dev* dev = wrapped_dev ? wrapped_dev->dev : NULL;
	int result = 0;

	if (!dev)
		return 0;

	if (fn->pre_visit_pci_dev) {
		result = fn->pre_visit_pci_dev(wrapped_dev, wrapped_parent);
		if (result)
			return result;
	}

	switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			result = pci_visit_bridge(fn, wrapped_dev,
						  wrapped_parent);
			if (result)
				return result;
			break;
		default:
			dbg("scanning device %02x, %02x\n",
			    PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
			if (fn->visit_pci_dev) {
				result = fn->visit_pci_dev (wrapped_dev,
							    wrapped_parent);
				if (result)
					return result;
			}
	}

	if (fn->post_visit_pci_dev)
		result = fn->post_visit_pci_dev(wrapped_dev, wrapped_parent);

	return result;
}


EXPORT_SYMBOL(pci_visit_dev);
EXPORT_SYMBOL(pci_read_config_byte_nodev);
EXPORT_SYMBOL(pci_read_config_word_nodev);
EXPORT_SYMBOL(pci_read_config_dword_nodev);
EXPORT_SYMBOL(pci_write_config_byte_nodev);
EXPORT_SYMBOL(pci_write_config_word_nodev);
EXPORT_SYMBOL(pci_write_config_dword_nodev);

