/*
 * drivers/base/fs.c - driver model interface to driverfs 
 *
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/stat.h>
#include <linux/limits.h>

extern struct driver_file_entry * device_default_files[];

/**
 * device_create_file - create a driverfs file for a device
 * @dev:	device requesting file
 * @entry:	entry describing file
 *
 * Allocate space for file entry, copy descriptor, and create.
 */
int device_create_file(struct device * dev, struct driver_file_entry * entry)
{
	int error = -EINVAL;

	if (dev) {
		get_device(dev);
		error = driverfs_create_file(entry,&dev->dir);
		put_device(dev);
	}
	return error;
}

/**
 * device_remove_file - remove a device's file by name
 * @dev:	device requesting removal
 * @name:	name of the file
 *
 */
void device_remove_file(struct device * dev, const char * name)
{
	if (dev) {
		get_device(dev);
		driverfs_remove_file(&dev->dir,name);
		put_device(dev);
	}
}

/**
 * device_remove_dir - remove a device's directory
 * @dev:	device in question
 */
void device_remove_dir(struct device * dev)
{
	if (dev)
		driverfs_remove_dir(&dev->dir);
}


static int get_devpath_length(struct device * dev)
{
	int length = 1;
	struct device * parent = dev;

	/* walk up the ancestors until we hit the root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		length += strlen(parent->bus_id) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

static void fill_devpath(struct device * dev, char * path, int length)
{
	struct device * parent;
	--length;
	for (parent = dev; parent; parent = parent->parent) {
		int cur = strlen(parent->bus_id);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(path + length,parent->bus_id,cur);
		*(path + --length) = '/';
	}

	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);
}

int device_bus_link(struct device * dev)
{
	char * path;
	int length;
	int error = 0;

	if (!dev->bus)
		return 0;

	length = get_devpath_length(dev);

	/* now add the path from the bus directory
	 * It should be '../../..' (one to get to the bus's directory,
	 * one to get to the 'bus' directory, and one to get to the root 
	 * of the fs.)
	 */
	length += strlen("../../..");

	if (length > PATH_MAX)
		return -ENAMETOOLONG;

	if (!(path = kmalloc(length,GFP_KERNEL)))
		return -ENOMEM;
	memset(path,0,length);

	/* our relative position */
	strcpy(path,"../../..");

	fill_devpath(dev,path,length);
	error = driverfs_create_symlink(&dev->bus->device_dir,dev->bus_id,path);
	kfree(path);
	return error;
}

int device_create_dir(struct driver_dir_entry * dir, struct driver_dir_entry * parent)
{
	dir->mode  = (S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO);
	return driverfs_create_dir(dir,parent);
}

/**
 * device_make_dir - create a driverfs directory
 * @name:	name of directory
 * @parent:	dentry for the parent directory
 *
 * Do the initial creation of the device's driverfs directory
 * and populate it with the one default file.
 *
 * This is just a helper for device_register(), as we
 * don't export this function. (Yes, that means we don't allow
 * devices to create subdirectories).
 */
int device_make_dir(struct device * dev)
{
	struct driver_dir_entry * parent = NULL;
	struct driver_file_entry * entry;
	int error;
	int i;

	if (dev->parent)
		parent = &dev->parent->dir;
	dev->dir.name = dev->bus_id;

	if ((error = device_create_dir(&dev->dir,parent)))
		return error;

	for (i = 0; (entry = *(device_default_files + i)); i++) {
		if ((error = device_create_file(dev,entry))) {
			device_remove_dir(dev);
			break;
		}
	}
	return error;
}

EXPORT_SYMBOL(device_create_file);
EXPORT_SYMBOL(device_remove_file);

