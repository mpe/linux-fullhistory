/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/init.h>
#include "base.h"

LIST_HEAD(global_device_list);

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

spinlock_t device_lock = SPIN_LOCK_UNLOCKED;

#define to_dev(node) container_of(node,struct device,driver_list)

static int probe(struct device * dev, struct device_driver * drv)
{
	dev->driver = drv;
	return drv->probe ? drv->probe(dev) : 0;
}

static void attach(struct device * dev)
{
	spin_lock(&device_lock);
	list_add_tail(&dev->driver_list,&dev->driver->devices);
	spin_unlock(&device_lock);
	devclass_add_device(dev);
}

/**
 * found_match - do actual binding of device to driver
 * @dev:	device 
 * @drv:	driver
 *
 * We're here because the bus's match callback returned success for this 
 * pair. We call the driver's probe callback to verify they're really a
 * match made in heaven.
 *
 * In the future, we may want to notify userspace of the binding. (But, 
 * we might not want to do it here).
 * 
 * We may also want to create a symlink in the driver's directory to the 
 * device's physical directory.
 */
static int found_match(struct device * dev, struct device_driver * drv)
{
	int error = 0;

	if (!(error = probe(dev,get_driver(drv)))) {
		pr_debug("bound device '%s' to driver '%s'\n",
			 dev->bus_id,drv->name);
		attach(dev);
	} else {
		put_driver(drv);
		dev->driver = NULL;
	}
	return error;
}

/**
 * device_attach - try to associated device with a driver
 * @drv:	current driver to try
 * @data:	device in disguise
 *
 * This function is used as a callback to bus_for_each_drv.
 * It calls the bus's match callback to check if the driver supports
 * the device. If so, it calls the found_match() function above to 
 * take care of all the details.
 */
static int do_device_attach(struct device_driver * drv, void * data)
{
	struct device * dev = (struct device *)data;
	int error = 0;

	if (drv->bus->match && drv->bus->match(dev,drv))
		error = found_match(dev,drv);
	return error;
}

static int device_attach(struct device * dev)
{
	int error = 0;
	if (!dev->driver) {
		if (dev->bus)
			error = bus_for_each_drv(dev->bus,dev,do_device_attach);
	} else
		attach(dev);
	return error;
}

static void device_detach(struct device * dev)
{
	struct device_driver * drv = dev->driver;

	if (drv) {
		devclass_remove_device(dev);
		if (drv && drv->remove)
			drv->remove(dev);
		dev->driver = NULL;
	}
}

static int do_driver_attach(struct device * dev, void * data)
{
	struct device_driver * drv = (struct device_driver *)data;
	int error = 0;

	if (!dev->driver) {
		if (dev->bus->match && dev->bus->match(dev,drv))
			error = found_match(dev,drv);
	}
	return error;
}

int driver_attach(struct device_driver * drv)
{
	return bus_for_each_dev(drv->bus,drv,do_driver_attach);
}

void driver_detach(struct device_driver * drv)
{
	struct list_head * node;
	struct device * prev = NULL;

	spin_lock(&device_lock);
	list_for_each(node,&drv->devices) {
		struct device * dev = get_device_locked(to_dev(node));
		if (dev) {
			if (prev)
				list_del_init(&prev->driver_list);
			spin_unlock(&device_lock);
			device_detach(dev);
			if (prev)
				put_device(prev);
			prev = dev;
			spin_lock(&device_lock);
		}
	}
	spin_unlock(&device_lock);
}

int device_add(struct device *dev)
{
	int error;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	spin_lock(&device_lock);
	dev->present = 1;
	if (dev->parent) {
		list_add_tail(&dev->g_list,&dev->parent->g_list);
		list_add_tail(&dev->node,&dev->parent->children);
	} else
		list_add_tail(&dev->g_list,&global_device_list);
	spin_unlock(&device_lock);

	pr_debug("DEV: registering device: ID = '%s', name = %s\n",
		 dev->bus_id, dev->name);

	if ((error = device_make_dir(dev)))
		goto register_done;

	bus_add_device(dev);

	/* bind to driver */
	device_attach(dev);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

	/* notify userspace of device entry */
	dev_hotplug(dev, "add");

 register_done:
	if (error) {
		spin_lock(&device_lock);
		list_del_init(&dev->g_list);
		list_del_init(&dev->node);
		spin_unlock(&device_lock);
	}
	return error;
}

void device_initialize(struct device *dev)
{
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->g_list);
	INIT_LIST_HEAD(&dev->driver_list);
	INIT_LIST_HEAD(&dev->bus_list);
	INIT_LIST_HEAD(&dev->intf_list);
	spin_lock_init(&dev->lock);
	atomic_set(&dev->refcount,1);
	if (dev->parent)
		get_device(dev->parent);
}

/**
 * device_register - register a device
 * @dev:	pointer to the device structure
 *
 * First, make sure that the device has a parent, create
 * a directory for it, then add it to the parent's list of
 * children.
 *
 * Maintains a global list of all devices, in depth-first ordering.
 * The head for that list is device_root.g_list.
 */
int device_register(struct device *dev)
{
	int error;

	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	device_initialize(dev);
	if (dev->parent)
		get_device(dev->parent);
	error = device_add(dev);
	if (error && dev->parent)
		put_device(dev->parent);
	return error;
}

struct device * get_device_locked(struct device * dev)
{
	struct device * ret = dev;
	if (dev && dev->present && atomic_read(&dev->refcount) > 0)
		atomic_inc(&dev->refcount);
	else
		ret = NULL;
	return ret;
}

struct device * get_device(struct device * dev)
{
	struct device * ret;
	spin_lock(&device_lock);
	ret = get_device_locked(dev);
	spin_unlock(&device_lock);
	return ret;
}

/**
 * put_device - decrement reference count, and clean up when it hits 0
 * @dev:	device in question
 */
void put_device(struct device * dev)
{
	struct device * parent;
	if (!atomic_dec_and_lock(&dev->refcount,&device_lock))
		return;
	parent = dev->parent;
	dev->parent = NULL;
	spin_unlock(&device_lock);

	BUG_ON(dev->present);

	if (dev->release)
		dev->release(dev);

	if (parent)
		put_device(parent);
}

void device_del(struct device * dev)
{
	spin_lock(&device_lock);
	dev->present = 0;
	list_del_init(&dev->node);
	list_del_init(&dev->g_list);
	list_del_init(&dev->bus_list);
	list_del_init(&dev->driver_list);
	spin_unlock(&device_lock);

	pr_debug("DEV: Unregistering device. ID = '%s', name = '%s'\n",
		 dev->bus_id,dev->name);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	/* notify userspace that this device is about to disappear */
	dev_hotplug (dev, "remove");

	device_detach(dev);
	bus_remove_device(dev);

	/* remove the driverfs directory */
	device_remove_dir(dev);
}

/**
 * device_unregister - unlink device
 * @dev:	device going away
 *
 * The device has been removed from the system, so we disavow knowledge
 * of it. It might not be the final reference to the device, so we mark
 * it as !present, so no more references to it can be acquired.
 * In the end, we decrement the final reference count for it.
 */
void device_unregister(struct device * dev)
{
	device_del(dev);
	put_device(dev);
}

static int __init device_init(void)
{
	int error;

	error = init_driverfs_fs();
	if (error)
		panic("DEV: could not initialize driverfs");
	return 0;
}

core_initcall(device_init);

EXPORT_SYMBOL(device_register);
EXPORT_SYMBOL(device_unregister);
EXPORT_SYMBOL(get_device);
EXPORT_SYMBOL(put_device);
