/*
 * bus.c - bus driver management
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 * 
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/init.h>
#include "base.h"

static LIST_HEAD(bus_driver_list);

static struct driver_dir_entry bus_dir = {
	name:	"bus",
	mode:	(S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO),
};

/**
 * bus_for_each_dev - walk list of devices and do something to each
 * @bus:	bus in question
 * @data:	data for the callback
 * @callback:	caller-defined action to perform on each device
 *
 * Why do we do this? So we can guarantee proper locking and reference
 * counting on devices as we touch each one.
 *
 * Algorithm:
 * Take the bus lock and get the first node in the list. We increment
 * the reference count and unlock the bus. If we have a device from a 
 * previous iteration, we decrement the reference count. 
 * After we call the callback, we get the next node in the list and loop.
 * At the end, if @dev is not null, we still have it pinned, so we need
 * to let it go.
 */
int bus_for_each_dev(struct bus_type * bus, void * data, 
		     int (*callback)(struct device * dev, void * data))
{
	struct device * next;
	struct device * dev = NULL;
	struct list_head * node;
	int error = 0;

	get_bus(bus);
	read_lock(&bus->lock);
	node = bus->devices.next;
	while (node != &bus->devices) {
		next = list_entry(node,struct device,bus_list);
		get_device(next);
		read_unlock(&bus->lock);

		if (dev)
			put_device(dev);
		dev = next;
		if ((error = callback(dev,data))) {
			put_device(dev);
			break;
		}
		read_lock(&bus->lock);
		node = dev->bus_list.next;
	}
	read_unlock(&bus->lock);
	if (dev)
		put_device(dev);
	put_bus(bus);
	return error;
}

int bus_for_each_drv(struct bus_type * bus, void * data,
		     int (*callback)(struct device_driver * drv, void * data))
{
	struct device_driver * next;
	struct device_driver * drv = NULL;
	struct list_head * node;
	int error = 0;

	/* pin bus in memory */
	get_bus(bus);

	read_lock(&bus->lock);
	node = bus->drivers.next;
	while (node != &bus->drivers) {
		next = list_entry(node,struct device_driver,bus_list);
		get_driver(next);
		read_unlock(&bus->lock);

		if (drv)
			put_driver(drv);
		drv = next;
		if ((error = callback(drv,data))) {
			put_driver(drv);
			break;
		}
		read_lock(&bus->lock);
		node = drv->bus_list.next;
	}
	read_unlock(&bus->lock);
	if (drv)
		put_driver(drv);
	put_bus(bus);
	return error;
}

/**
 * bus_add_device - add device to bus
 * @dev:	device being added
 *
 * Add the device to its bus's list of devices.
 * Create a symlink in the bus's 'devices' directory to the 
 * device's physical location.
 * Try and bind the device to a driver.
 */
int bus_add_device(struct device * dev)
{
	if (dev->bus) {
		pr_debug("registering %s with bus '%s'\n",dev->bus_id,dev->bus->name);
		get_bus(dev->bus);
		write_lock(&dev->bus->lock);
		list_add_tail(&dev->bus_list,&dev->bus->devices);
		write_unlock(&dev->bus->lock);
		device_bus_link(dev);
	}
	return 0;
}

/**
 * bus_remove_device - remove device from bus
 * @dev:	device to be removed
 *
 * Remove symlink from bus's directory.
 * Delete device from bus's list.
 */
void bus_remove_device(struct device * dev)
{
	if (dev->bus) {
		driverfs_remove_file(&dev->bus->device_dir,dev->bus_id);
		write_lock(&dev->bus->lock);
		list_del_init(&dev->bus_list);
		write_unlock(&dev->bus->lock);
		put_bus(dev->bus);
	}
}

static int bus_make_dir(struct bus_type * bus)
{
	int error;
	bus->dir.name = bus->name;

	error = device_create_dir(&bus->dir,&bus_dir);
	if (!error) {
		bus->device_dir.name = "devices";
		device_create_dir(&bus->device_dir,&bus->dir);

		bus->driver_dir.name = "drivers";
		device_create_dir(&bus->driver_dir,&bus->dir);
	}
	return error;
}


int bus_register(struct bus_type * bus)
{
	spin_lock(&device_lock);
	rwlock_init(&bus->lock);
	INIT_LIST_HEAD(&bus->devices);
	INIT_LIST_HEAD(&bus->drivers);
	list_add_tail(&bus->node,&bus_driver_list);
	atomic_set(&bus->refcount,2);
	spin_unlock(&device_lock);

	pr_debug("bus type '%s' registered\n",bus->name);

	/* give it some driverfs entities */
	bus_make_dir(bus);
	put_bus(bus);

	return 0;
}

void put_bus(struct bus_type * bus)
{
	if (!atomic_dec_and_lock(&bus->refcount,&device_lock))
		return;
	list_del_init(&bus->node);
	spin_unlock(&device_lock);

	/* remove driverfs entries */
	driverfs_remove_dir(&bus->driver_dir);
	driverfs_remove_dir(&bus->device_dir);
	driverfs_remove_dir(&bus->dir);
}

static int __init bus_init(void)
{
	/* make 'bus' driverfs directory */
	return driverfs_create_dir(&bus_dir,NULL);
}

core_initcall(bus_init);

EXPORT_SYMBOL(bus_for_each_dev);
EXPORT_SYMBOL(bus_for_each_drv);
EXPORT_SYMBOL(bus_add_device);
EXPORT_SYMBOL(bus_remove_device);
EXPORT_SYMBOL(bus_register);
EXPORT_SYMBOL(put_bus);
