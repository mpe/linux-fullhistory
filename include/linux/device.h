/*
 * device.h - generic, centralized driver model
 *
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 *
 * This is a relatively simple centralized driver model.
 * The data structures were mainly lifted directly from the PCI
 * driver model. These are thought to be the common fields that
 * are relevant to all device buses.
 *
 * All the devices are arranged in a tree. All devices should
 * have some sort of parent bus of whom they are children of.
 * Devices should not be direct children of the system root.
 *
 * Device drivers should not directly call the device_* routines
 * or access the contents of struct device directly. Instead,
 * abstract that from the drivers and write bus-specific wrappers
 * that do it for you.
 *
 * See Documentation/driver-model.txt for more information.
 */

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/types.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/driverfs_fs.h>

#define DEVICE_NAME_SIZE	80
#define DEVICE_ID_SIZE		32
#define BUS_ID_SIZE		16


enum {
	SUSPEND_NOTIFY,
	SUSPEND_SAVE_STATE,
	SUSPEND_DISABLE,
	SUSPEND_POWER_DOWN,
};

enum {
	RESUME_POWER_ON,
	RESUME_RESTORE_STATE,
	RESUME_ENABLE,
};

enum {
	REMOVE_NOTIFY,
	REMOVE_FREE_RESOURCES,
};

struct device;


struct bus_type {
	char			* name;
	rwlock_t		lock;
	atomic_t		refcount;

	list_t			node;
	list_t			devices;
	list_t			drivers;

	struct driver_dir_entry	dir;
	struct driver_dir_entry	device_dir;
	struct driver_dir_entry	driver_dir;
};


extern int bus_register(struct bus_type * bus);

static inline struct bus_type * get_bus(struct bus_type * bus)
{
	BUG_ON(!atomic_read(&bus->refcount));
	atomic_inc(&bus->refcount);
	return bus;
}

extern void put_bus(struct bus_type * bus);


struct device_driver {
	char			* name;
	struct bus_type		* bus;

	rwlock_t		lock;
	atomic_t		refcount;

	list_t			bus_list;
	list_t			devices;

	struct driver_dir_entry	dir;

	int	(*probe)	(struct device * dev);
	int 	(*remove)	(struct device * dev, u32 flags);

	int	(*suspend)	(struct device * dev, u32 state, u32 level);
	int	(*resume)	(struct device * dev, u32 level);

	void	(*release)	(struct device_driver * drv);
};



extern int driver_register(struct device_driver * drv);

static inline struct device_driver * get_driver(struct device_driver * drv)
{
	BUG_ON(!atomic_read(&drv->refcount));
	atomic_inc(&drv->refcount);
	return drv;
}

extern void put_driver(struct device_driver * drv);

extern int driver_for_each_dev(struct device_driver * drv, void * data, 
			       int (*callback)(struct device * dev, void * data));


struct device {
	struct list_head g_list;        /* node in depth-first order list */
	struct list_head node;		/* node in sibling list */
	struct list_head bus_list;	/* node in bus's list */
	struct list_head driver_list;
	struct list_head children;
	struct device 	* parent;

	char	name[DEVICE_NAME_SIZE];	/* descriptive ascii string */
	char	bus_id[BUS_ID_SIZE];	/* position on parent bus */

	spinlock_t	lock;		/* lock for the device to ensure two
					   different layers don't access it at
					   the same time. */
	atomic_t	refcount;	/* refcount to make sure the device
					 * persists for the right amount of time */

	struct bus_type	* bus;		/* type of bus device is on */
	struct driver_dir_entry	dir;

	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*driver_data;	/* data private to the driver */
	void		*platform_data;	/* Platform specific data (e.g. ACPI,
					   BIOS data relevant to device) */

	u32		current_state;  /* Current operating state. In
					   ACPI-speak, this is D0-D3, D0
					   being fully functional, and D3
					   being off. */

	unsigned char *saved_state;	/* saved device state */

	void	(*release)(struct device * dev);
};

static inline struct device *
list_to_dev(struct list_head *node)
{
	return list_entry(node, struct device, node);
}

static inline struct device *
g_list_to_dev(struct list_head *g_list)
{
	return list_entry(g_list, struct device, g_list);
}

/*
 * High level routines for use by the bus drivers
 */
extern int device_register(struct device * dev);

extern int device_create_file(struct device *device, struct driver_file_entry * entry);
extern void device_remove_file(struct device * dev, const char * name);

/*
 * Platform "fixup" functions - allow the platform to have their say
 * about devices and actions that the general device layer doesn't
 * know about.
 */
/* Notify platform of device discovery */
extern int (*platform_notify)(struct device * dev);

extern int (*platform_notify_remove)(struct device * dev);

/* device and bus locking helpers.
 *
 * FIXME: Is there anything else we need to do?
 */
static inline void lock_device(struct device * dev)
{
	spin_lock(&dev->lock);
}

static inline void unlock_device(struct device * dev)
{
	spin_unlock(&dev->lock);
}

/**
 * get_device - atomically increment the reference count for the device.
 *
 */
static inline void get_device(struct device * dev)
{
	BUG_ON(!atomic_read(&dev->refcount));
	atomic_inc(&dev->refcount);
}

extern void put_device(struct device * dev);

/* drivers/base/sys.c */
extern int register_sys_device(struct device * dev);
extern void unregister_sys_device(struct device * dev);

/* drivers/base/power.c */
extern int device_suspend(u32 state, u32 level);
extern void device_resume(u32 level);
extern void device_shutdown(void);

#endif /* _DEVICE_H_ */
