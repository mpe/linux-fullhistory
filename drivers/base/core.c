/*
 * drivers/base/core.c - core driver model code (device registration, etc)
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/semaphore.h>

#include "base.h"

LIST_HEAD(global_device_list);

int (*platform_notify)(struct device * dev) = NULL;
int (*platform_notify_remove)(struct device * dev) = NULL;

DECLARE_MUTEX(device_sem);

spinlock_t device_lock = SPIN_LOCK_UNLOCKED;

#define to_dev(obj) container_of(obj,struct device,kobj)


/*
 * sysfs bindings for devices.
 */

#define to_dev_attr(_attr) container_of(_attr,struct device_attribute,attr)

extern struct attribute * dev_default_attrs[];

static ssize_t
dev_attr_show(struct kobject * kobj, struct attribute * attr,
	      char * buf, size_t count, loff_t off)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->show)
		ret = dev_attr->show(dev,buf,count,off);
	return ret;
}

static ssize_t
dev_attr_store(struct kobject * kobj, struct attribute * attr,
	       const char * buf, size_t count, loff_t off)
{
	struct device_attribute * dev_attr = to_dev_attr(attr);
	struct device * dev = to_dev(kobj);
	ssize_t ret = 0;

	if (dev_attr->store)
		ret = dev_attr->store(dev,buf,count,off);
	return ret;
}

static struct sysfs_ops dev_sysfs_ops = {
	.show	= dev_attr_show,
	.store	= dev_attr_store,
};


/**
 *	device_release - free device structure.
 *	@kobj:	device's kobject.
 *
 *	This is called once the reference count for the object
 *	reaches 0. We forward the call to the device's release
 *	method, which should handle actually freeing the structure.
 */
static void device_release(struct kobject * kobj)
{
	struct device * dev = to_dev(kobj);
	if (dev->release)
		dev->release(dev);
}


/**
 *	device_subsys - structure to be registered with kobject core.
 */
struct subsystem device_subsys = {
	.kobj		= {
		.name	= "devices",
	},
	.release	= device_release,
	.sysfs_ops	= &dev_sysfs_ops,
	.default_attrs	= dev_default_attrs,
};


/**
 *	device_create_file - create sysfs attribute file for device.
 *	@dev:	device.
 *	@attr:	device attribute descriptor.
 */

int device_create_file(struct device * dev, struct device_attribute * attr)
{
	int error = 0;
	if (get_device(dev)) {
		error = sysfs_create_file(&dev->kobj,&attr->attr);
		put_device(dev);
	}
	return error;
}

/**
 *	device_remove_file - remove sysfs attribute file.
 *	@dev:	device.
 *	@attr:	device attribute descriptor.
 */

void device_remove_file(struct device * dev, struct device_attribute * attr)
{
	if (get_device(dev)) {
		sysfs_remove_file(&dev->kobj,&attr->attr);
		put_device(dev);
	}
}


/**
 *	device_initialize - init device structure.
 *	@dev:	device.
 *
 *	This prepares the device for use by other layers,
 *	including adding it to the device hierarchy. 
 *	It is the first half of device_register(), if called by
 *	that, though it can also be called separately, so one
 *	may use @dev's fields (e.g. the refcount).
 */

void device_initialize(struct device *dev)
{
	kobject_init(&dev->kobj);
	INIT_LIST_HEAD(&dev->node);
	INIT_LIST_HEAD(&dev->children);
	INIT_LIST_HEAD(&dev->g_list);
	INIT_LIST_HEAD(&dev->driver_list);
	INIT_LIST_HEAD(&dev->bus_list);
	INIT_LIST_HEAD(&dev->intf_list);
//	spin_lock_init(&dev->lock);
}

/**
 *	device_add - add device to device hierarchy.
 *	@dev:	device.
 *
 *	This is part 2 of device_register(), though may be called 
 *	separately _iff_ device_initialize() has been called separately.
 *
 *	This adds it to the kobject hierarchy via kobject_add(), adds it
 *	to the global and sibling lists for the device, then
 *	adds it to the other relevant subsystems of the driver model.
 */
int device_add(struct device *dev)
{
	struct device * parent;
	int error;

	dev = get_device(dev);
	if (!dev || !strlen(dev->bus_id))
		return -EINVAL;

	parent = get_device(dev->parent);

	pr_debug("DEV: registering device: ID = '%s', name = %s\n",
		 dev->bus_id, dev->name);

	/* first, register with generic layer. */
	strncpy(dev->kobj.name,dev->bus_id,KOBJ_NAME_LEN);
	dev->kobj.subsys = &device_subsys;
	if (parent)
		dev->kobj.parent = &parent->kobj;

	if ((error = kobject_add(&dev->kobj)))
		goto register_done;

	/* now take care of our own registration */
	down(&device_sem);
	if (parent) {
		list_add_tail(&dev->g_list,&dev->parent->g_list);
		list_add_tail(&dev->node,&parent->children);
	} else
		list_add_tail(&dev->g_list,&global_device_list);
	up(&device_sem);

	bus_add_device(dev);

	/* notify platform of device entry */
	if (platform_notify)
		platform_notify(dev);

	/* notify userspace of device entry */
	dev_hotplug(dev, "add");

	devclass_add_device(dev);
 register_done:
	if (error && parent)
		put_device(parent);
	put_device(dev);
	return error;
}


/**
 *	device_register - register a device with the system.
 *	@dev:	pointer to the device structure
 *
 *	This happens in two clean steps - initialize the device
 *	and add it to the system. The two steps can be called 
 *	separately, but this is the easiest and most common. 
 *	I.e. you should only call the two helpers separately if 
 *	have a clearly defined need to use and refcount the device
 *	before it is added to the hierarchy.
 */

int device_register(struct device *dev)
{
	device_initialize(dev);
	return device_add(dev);
}


/**
 *	get_device - increment reference count for device.
 *	@dev:	device.
 *
 *	This simply forwards the call to kobject_get(), though
 *	we do take care to provide for the case that we get a NULL
 *	pointer passed in.
 */

struct device * get_device(struct device * dev)
{
	return dev ? to_dev(kobject_get(&dev->kobj)) : NULL;
}


/**
 *	put_device - decrement reference count.
 *	@dev:	device in question.
 */
void put_device(struct device * dev)
{
	kobject_put(&dev->kobj);
}


/**
 *	device_del - delete device from system.
 *	@dev:	device.
 *
 *	This is the first part of the device unregistration 
 *	sequence. This removes the device from the lists we control
 *	from here, has it removed from the other driver model 
 *	subsystems it was added to in device_add(), and removes it
 *	from the kobject hierarchy.
 *
 *	NOTE: this should be called manually _iff_ device_add() was 
 *	also called manually.
 */

void device_del(struct device * dev)
{
	struct device * parent = dev->parent;

	down(&device_sem);
	list_del_init(&dev->node);
	list_del_init(&dev->g_list);
	up(&device_sem);

	/* Notify the platform of the removal, in case they
	 * need to do anything...
	 */
	if (platform_notify_remove)
		platform_notify_remove(dev);

	/* notify userspace that this device is about to disappear */
	dev_hotplug (dev, "remove");

	bus_remove_device(dev);

	kobject_del(&dev->kobj);

	if (parent)
		put_device(parent);

}

/**
 *	device_unregister - unregister device from system.
 *	@dev:	device going away.
 *
 *	We do this in two parts, like we do device_register(). First,
 *	we remove it from all the subsystems with device_del(), then
 *	we decrement the reference count via put_device(). If that
 *	is the final reference count, the device will be cleaned up
 *	via device_release() above. Otherwise, the structure will 
 *	stick around until the final reference to the device is dropped.
 */
void device_unregister(struct device * dev)
{
	pr_debug("DEV: Unregistering device. ID = '%s', name = '%s'\n",
		 dev->bus_id,dev->name);
	device_del(dev);
	put_device(dev);
}

static int __init device_subsys_init(void)
{
	return subsystem_register(&device_subsys);
}

core_initcall(device_subsys_init);

EXPORT_SYMBOL(device_initialize);
EXPORT_SYMBOL(device_add);
EXPORT_SYMBOL(device_register);

EXPORT_SYMBOL(device_del);
EXPORT_SYMBOL(device_unregister);
EXPORT_SYMBOL(get_device);
EXPORT_SYMBOL(put_device);

EXPORT_SYMBOL(device_create_file);
EXPORT_SYMBOL(device_remove_file);
